package org.gowandroid.app;

import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;

import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Custom God of War themed launcher for the Android native port.
 *
 * It selects a user-owned disc image and validates its ISO9660 primary
 * volume descriptor. It does not yet identify SYSTEM.CNF or boot the game.
 */
public final class MainActivity extends AppCompatActivity {
    private static final String PREF_DISC_URI = "selected_disc_uri";
    private static final int ISO_SECTOR_SIZE = 2048;
    private static final long PRIMARY_VOLUME_DESCRIPTOR_OFFSET = 16L * ISO_SECTOR_SIZE;

    private static final int COLOR_BACKGROUND = Color.rgb(10, 8, 10);
    private static final int COLOR_RED = Color.rgb(172, 39, 31);
    private static final int COLOR_RED_BRIGHT = Color.rgb(222, 71, 47);
    private static final int COLOR_BRONZE = Color.rgb(190, 145, 83);
    private static final int COLOR_TEXT = Color.rgb(237, 224, 204);
    private static final int COLOR_MUTED = Color.rgb(164, 151, 139);

    private final ExecutorService ioExecutor = Executors.newSingleThreadExecutor();

    private TextView discName;
    private TextView discStatus;
    private Button selectDiscButton;

    private Uri selectedDiscUri;
    private boolean selectedDiscValid;

    private final ActivityResultLauncher<String[]> openDocumentLauncher =
            registerForActivityResult(
                    new ActivityResultContracts.OpenDocument(),
                    this::onDiscSelected);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        applyImmersiveMode();
        buildLauncherUi();
        restoreSelectedDisc();
    }

    private void applyImmersiveMode() {
        Window window = getWindow();
        window.setStatusBarColor(COLOR_BACKGROUND);
        window.setNavigationBarColor(COLOR_BACKGROUND);
        window.addFlags(
                android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        window.getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    private void buildLauncherUi() {
        FrameLayout root = new FrameLayout(this);
        root.setBackground(gradient(
                new int[]{0xFF080709, 0xFF1A0C0D, 0xFF080709},
                GradientDrawable.Orientation.TL_BR,
                0,
                0));

        ImageView artwork = new ImageView(this);
        artwork.setImageResource(R.drawable.ic_launcher_foreground);
        artwork.setScaleType(ImageView.ScaleType.CENTER_CROP);
        artwork.setAlpha(0.70f);
        FrameLayout.LayoutParams artParams = new FrameLayout.LayoutParams(
                getScreenWidthPx() * 46 / 100,
                ViewGroup.LayoutParams.MATCH_PARENT,
                Gravity.END | Gravity.CENTER_VERTICAL);
        root.addView(artwork, artParams);

        View rightScrim = new View(this);
        rightScrim.setBackground(new GradientDrawable(
                GradientDrawable.Orientation.RIGHT_LEFT,
                new int[]{0x08000000, 0x55000000, 0xB5080608}));
        root.addView(rightScrim, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        int panelWidth = Math.max(dp(320), (int) (getScreenWidthPx() * 0.62f));
        panelWidth = Math.min(panelWidth, (int) (getScreenWidthPx() * 0.72f));

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(22), dp(15), dp(22), dp(12));
        panel.setBackground(gradient(
                new int[]{0xF20B0809, 0xE8110B0C, 0xB80C080A},
                GradientDrawable.Orientation.LEFT_RIGHT,
                dp(2),
                0xFF76502F));

        FrameLayout.LayoutParams panelParams = new FrameLayout.LayoutParams(
                panelWidth,
                ViewGroup.LayoutParams.MATCH_PARENT,
                Gravity.START | Gravity.CENTER_VERTICAL);
        root.addView(panel, panelParams);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        panel.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(Gravity.CENTER_VERTICAL);
        scroll.addView(content, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        content.addView(createHeader(), matchWrap());
        content.addView(createDivider(), topMargin(12));

        TextView tagline = text(
                "A VINGANÇA DE UM DEUS... AGORA EM SUAS MÃOS.",
                10,
                COLOR_BRONZE,
                Typeface.create("serif", Typeface.BOLD));
        tagline.setLetterSpacing(0.10f);
        content.addView(tagline, topMargin(10));

        TextView section = text(
                "SUA JORNADA COMEÇA AQUI",
                10,
                COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        section.setLetterSpacing(0.16f);
        content.addView(section, topMargin(16));

        Button playButton = menuButton("Ω   JOGAR", true);
        playButton.setOnClickListener(view -> onPlayPressed());
        content.addView(playButton, topMargin(7));

        selectDiscButton = menuButton("▣   SELECIONAR IMAGEM DO DISCO", false);
        selectDiscButton.setOnClickListener(
                view -> openDocumentLauncher.launch(new String[]{"*/*"}));
        content.addView(selectDiscButton, topMargin(7));

        Button vulkanButton = menuButton("◇   TESTAR APRESENTAÇÃO VULKAN", false);
        vulkanButton.setOnClickListener(
                view -> startActivity(new Intent(this, GowGameActivity.class)));
        content.addView(vulkanButton, topMargin(7));

        content.addView(createDiscStatusCard(), topMargin(12));

        LinearLayout utilityRow = new LinearLayout(this);
        utilityRow.setOrientation(LinearLayout.HORIZONTAL);
        utilityRow.setGravity(Gravity.CENTER_VERTICAL);

        Button settingsButton = utilityButton("CONFIGURAÇÕES");
        settingsButton.setOnClickListener(view -> showSettingsDialog());
        utilityRow.addView(settingsButton, new LinearLayout.LayoutParams(
                0, dp(38), 1f));

        View spacer = new View(this);
        utilityRow.addView(spacer, new LinearLayout.LayoutParams(dp(8), 1));

        Button aboutButton = utilityButton("SOBRE");
        aboutButton.setOnClickListener(view -> showAboutDialog());
        utilityRow.addView(aboutButton, new LinearLayout.LayoutParams(
                0, dp(38), 1f));

        Button exitButton = utilityButton("SAIR");
        exitButton.setOnClickListener(view -> finish());
        utilityRow.addView(exitButton, new LinearLayout.LayoutParams(
                0, dp(38), 0.65f));

        content.addView(utilityRow, topMargin(10));

        TextView footer = text(
                "GOW_ANDROID  •  PORT NATIVO PARA ANDROID  •  V0.1.0-DEV",
                8,
                0xFF87766D,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        footer.setGravity(Gravity.CENTER);
        footer.setLetterSpacing(0.05f);
        content.addView(footer, topMargin(9));

        setContentView(root);
    }

    private View createHeader() {
        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);

        ImageView icon = new ImageView(this);
        icon.setImageResource(R.drawable.ic_launcher_foreground);
        icon.setScaleType(ImageView.ScaleType.CENTER_CROP);
        icon.setContentDescription("Emblema do projeto God of War Android");
        icon.setBackground(gradient(
                new int[]{0xFF1E0A0B, 0xFF080708},
                GradientDrawable.Orientation.TL_BR,
                dp(1),
                COLOR_BRONZE));
        header.addView(icon, new LinearLayout.LayoutParams(dp(52), dp(52)));

        LinearLayout titleStack = new LinearLayout(this);
        titleStack.setOrientation(LinearLayout.VERTICAL);
        titleStack.setPadding(dp(12), 0, 0, 0);

        TextView title = text(
                "GOD OF WAR",
                25,
                COLOR_TEXT,
                Typeface.create("serif", Typeface.BOLD));
        title.setLetterSpacing(0.02f);
        titleStack.addView(title, matchWrap());

        TextView subtitle = text(
                "ANDROID NATIVE PORT",
                9,
                COLOR_BRONZE,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        subtitle.setLetterSpacing(0.21f);
        LinearLayout.LayoutParams subtitleParams = matchWrap();
        subtitleParams.topMargin = dp(2);
        titleStack.addView(subtitle, subtitleParams);

        header.addView(titleStack, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        TextView build = text(
                "v0.1.0\nDEV",
                9,
                COLOR_MUTED,
                Typeface.create("monospace", Typeface.NORMAL));
        build.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        header.addView(build, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        return header;
    }

    private View createDivider() {
        View divider = new View(this);
        divider.setBackground(new GradientDrawable(
                GradientDrawable.Orientation.LEFT_RIGHT,
                new int[]{COLOR_RED, COLOR_BRONZE, 0x2276502F}));
        divider.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(2)));
        return divider;
    }

    private View createDiscStatusCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(13), dp(9), dp(13), dp(9));
        card.setBackground(gradient(
                new int[]{0xD91A1112, 0xC90F0B0C},
                GradientDrawable.Orientation.TOP_BOTTOM,
                dp(1),
                0xFF65452D));

        TextView label = text(
                "ESTADO DA IMAGEM",
                8,
                COLOR_BRONZE,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        label.setLetterSpacing(0.14f);
        card.addView(label, matchWrap());

        discName = text(
                "Nenhuma imagem selecionada",
                12,
                COLOR_TEXT,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        LinearLayout.LayoutParams nameParams = matchWrap();
        nameParams.topMargin = dp(4);
        card.addView(discName, nameParams);

        discStatus = text(
                "Selecione sua imagem de God of War para verificar o formato.",
                10,
                COLOR_MUTED,
                Typeface.create("sans-serif", Typeface.NORMAL));
        discStatus.setLineSpacing(dp(2), 1f);
        LinearLayout.LayoutParams statusParams = matchWrap();
        statusParams.topMargin = dp(3);
        card.addView(discStatus, statusParams);

        return card;
    }

    private Button menuButton(String label, boolean primary) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setGravity(Gravity.CENTER_VERTICAL | Gravity.START);
        button.setTextSize(12);
        button.setLetterSpacing(0.045f);
        button.setTypeface(Typeface.create(
                "serif", primary ? Typeface.BOLD : Typeface.NORMAL));
        button.setTextColor(primary ? Color.WHITE : COLOR_TEXT);
        button.setPadding(dp(15), 0, dp(12), 0);
        button.setMinHeight(dp(primary ? 48 : 43));
        button.setStateListAnimator(null);
        button.setFocusable(true);
        button.setBackground(menuButtonBackground(primary, false));
        button.setOnFocusChangeListener((view, focused) ->
                button.setBackground(menuButtonBackground(primary, focused)));
        return button;
    }

    private Button utilityButton(String label) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextSize(9);
        button.setLetterSpacing(0.04f);
        button.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        button.setTextColor(COLOR_BRONZE);
        button.setPadding(dp(4), 0, dp(4), 0);
        button.setMinHeight(dp(36));
        button.setStateListAnimator(null);
        button.setBackground(gradient(
                new int[]{0xFF211719, 0xFF100C0D},
                GradientDrawable.Orientation.TOP_BOTTOM,
                dp(1),
                0xFF57402D));
        return button;
    }

    private GradientDrawable menuButtonBackground(boolean primary, boolean focused) {
        int[] colors = primary || focused
                ? new int[]{0xFF8D211D, 0xFF3F1114}
                : new int[]{0xFF302326, 0xFF171012};
        int stroke = primary || focused ? COLOR_RED_BRIGHT : 0xFF6E4B35;
        return gradient(
                colors,
                GradientDrawable.Orientation.LEFT_RIGHT,
                dp(focused ? 2 : 1),
                stroke);
    }

    private GradientDrawable gradient(
            int[] colors,
            GradientDrawable.Orientation orientation,
            int strokeWidth,
            int strokeColor) {
        GradientDrawable drawable = new GradientDrawable(orientation, colors);
        drawable.setCornerRadius(dp(4));
        if (strokeWidth > 0) {
            drawable.setStroke(strokeWidth, strokeColor);
        }
        return drawable;
    }

    private TextView text(String value, float size, int color, Typeface typeface) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextColor(color);
        view.setTextSize(size);
        view.setTypeface(typeface);
        return view;
    }

    private void onPlayPressed() {
        if (selectedDiscUri == null) {
            new AlertDialog.Builder(this)
                    .setTitle("Nenhuma imagem selecionada")
                    .setMessage("Escolha a imagem do seu disco de God of War para continuar.")
                    .setPositiveButton("Selecionar disco",
                            (dialog, which) ->
                                    openDocumentLauncher.launch(new String[]{"*/*"}))
                    .setNegativeButton("Cancelar", null)
                    .show();
            return;
        }

        String message = selectedDiscValid
                ? "O descritor ISO9660 foi encontrado, mas o launcher ainda não identificou "
                        + "o SYSTEM.CNF nem integrou o runtime necessário para iniciar God of War. "
                        + "A imagem foi selecionada; a execução do jogo ainda está em desenvolvimento."
                : "A imagem selecionada ainda não passou pela validação ISO9660. "
                        + "Selecione outro arquivo ou tente novamente.";

        new AlertDialog.Builder(this)
                .setTitle(selectedDiscValid ? "Runtime em desenvolvimento" : "Imagem não validada")
                .setMessage(message)
                .setPositiveButton("Entendi", null)
                .setNeutralButton("Trocar imagem",
                        (dialog, which) ->
                                openDocumentLauncher.launch(new String[]{"*/*"}))
                .show();
    }

    private void showSettingsDialog() {
        new AlertDialog.Builder(this)
                .setTitle("Configurações")
                .setMessage(
                        "PLATAFORMA\nAndroid / ARM64\n\n"
                                + "GRÁFICOS\nVulkan (teste de apresentação)\n\n"
                                + "VERSÃO\n0.1.0-dev\n\n"
                                + "As opções de resolução, controles e áudio serão adicionadas "
                                + "quando os respectivos subsistemas estiverem integrados.")
                .setPositiveButton("Fechar", null)
                .show();
    }

    private void showAboutDialog() {
        new AlertDialog.Builder(this)
                .setTitle("Sobre o projeto")
                .setMessage(
                        "Gow_android é um projeto independente que busca construir um port "
                                + "nativo de God of War (2005) para Android.\n\n"
                                + "O launcher, a seleção de disco e o teste Vulkan são etapas "
                                + "iniciais. O jogo ainda não é executável nesta versão.\n\n"
                                + "Referências técnicas e créditos: CodeAbhi826, KIexster, "
                                + "Waffle565, ran-j / PS2Recomp e PCSX2. Consulte "
                                + "THIRD_PARTY_NOTICES.md no repositório.")
                .setPositiveButton("Fechar", null)
                .show();
    }

    private void onDiscSelected(Uri uri) {
        if (uri == null) {
            return;
        }

        selectedDiscUri = uri;
        selectedDiscValid = false;

        try {
            getContentResolver().takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // Some document providers grant temporary access only.
        }

        getPreferences(MODE_PRIVATE)
                .edit()
                .putString(PREF_DISC_URI, uri.toString())
                .apply();

        validateAndDisplay(uri);
    }

    private void restoreSelectedDisc() {
        String savedUri = getPreferences(MODE_PRIVATE)
                .getString(PREF_DISC_URI, null);
        if (savedUri == null || savedUri.isEmpty()) {
            return;
        }
        selectedDiscUri = Uri.parse(savedUri);
        validateAndDisplay(selectedDiscUri);
    }

    private void validateAndDisplay(Uri uri) {
        if (selectDiscButton != null) {
            selectDiscButton.setEnabled(false);
        }
        discName.setText("Verificando imagem...");
        discStatus.setText("Lendo o descritor primário ISO9660.");
        discStatus.setTextColor(COLOR_BRONZE);
        selectedDiscValid = false;

        ioExecutor.execute(() -> {
            String name = queryDisplayName(uri);
            String result;
            boolean valid = false;

            try {
                validateIso9660(uri);
                valid = true;
                result = "ISO9660 válido. Serial e revisão ainda não identificados.";
            } catch (IOException | SecurityException exception) {
                result = "Falha na validação: " + safeMessage(exception);
            }

            final boolean isValid = valid;
            final String finalName = name;
            final String finalResult = result;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) {
                    return;
                }
                selectedDiscValid = isValid;
                discName.setText(finalName);
                discStatus.setText(finalResult);
                discStatus.setTextColor(
                        isValid ? Color.rgb(139, 190, 140) : Color.rgb(222, 113, 96));
                if (selectDiscButton != null) {
                    selectDiscButton.setEnabled(true);
                }
            });
        });
    }

    private void validateIso9660(Uri uri) throws IOException {
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            if (input == null) {
                throw new IOException("O provedor não conseguiu abrir o arquivo.");
            }

            skipFully(input, PRIMARY_VOLUME_DESCRIPTOR_OFFSET);
            byte[] descriptor = new byte[ISO_SECTOR_SIZE];
            readFully(input, descriptor);

            if ((descriptor[0] & 0xFF) != 1
                    || descriptor[1] != 'C'
                    || descriptor[2] != 'D'
                    || descriptor[3] != '0'
                    || descriptor[4] != '0'
                    || descriptor[5] != '1'
                    || (descriptor[6] & 0xFF) != 1) {
                throw new IOException(
                        "O setor 16 não contém um descritor primário ISO9660 válido.");
            }
        }
    }

    private static void skipFully(InputStream input, long byteCount) throws IOException {
        long remaining = byteCount;
        while (remaining > 0) {
            long skipped = input.skip(remaining);
            if (skipped > 0) {
                remaining -= skipped;
                continue;
            }
            if (input.read() == -1) {
                throw new IOException("Arquivo muito pequeno para conter o descritor ISO9660.");
            }
            remaining--;
        }
    }

    private static void readFully(InputStream input, byte[] buffer) throws IOException {
        int offset = 0;
        while (offset < buffer.length) {
            int count = input.read(buffer, offset, buffer.length - offset);
            if (count < 0) {
                throw new IOException("Fim inesperado ao ler o descritor ISO9660.");
            }
            if (count > 0) {
                offset += count;
            }
        }
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(
                uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    String value = cursor.getString(index);
                    if (value != null && !value.trim().isEmpty()) {
                        return value;
                    }
                }
            }
        } catch (RuntimeException ignored) {
            // A provider is allowed to omit its display name.
        }
        return "Imagem de disco selecionada";
    }

    private static String safeMessage(Exception exception) {
        String message = exception.getMessage();
        return message == null || message.trim().isEmpty()
                ? exception.getClass().getSimpleName()
                : message;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams topMargin(int dpValue) {
        LinearLayout.LayoutParams params = matchWrap();
        params.topMargin = dp(dpValue);
        return params;
    }

    private int getScreenWidthPx() {
        return getResources().getDisplayMetrics().widthPixels;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    protected void onDestroy() {
        ioExecutor.shutdownNow();
        super.onDestroy();
    }
}

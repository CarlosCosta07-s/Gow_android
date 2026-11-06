package org.gowandroid.app;

import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.system.ErrnoException;
import android.system.Os;
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
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * FORGE is the minimalist game-library interface for the Gow_android project.
 *
 * It uses original UI elements only: no character portraits, screenshots or
 * extracted artwork from God of War. Disc selection and ISO9660 validation are
 * implemented, but SYSTEM.CNF identification and game boot are not yet available.
 */
public final class MainActivity extends AppCompatActivity {
    private static final String PREF_DISC_URI = "selected_disc_uri";
    private static final int ISO_SECTOR_SIZE = 2048;
    private static final long PRIMARY_VOLUME_DESCRIPTOR_OFFSET = 16L * ISO_SECTOR_SIZE;

    private static final int COLOR_BACKGROUND = 0xFF0D0F12;
    private static final int COLOR_SIDEBAR = 0xFF101317;
    private static final int COLOR_SURFACE = 0xFF171B20;
    private static final int COLOR_SURFACE_RAISED = 0xFF1D2229;
    private static final int COLOR_RED = 0xFFC5222D;
    private static final int COLOR_RED_BRIGHT = 0xFFE3424A;
    private static final int COLOR_TEXT = 0xFFF2F1EF;
    private static final int COLOR_MUTED = 0xFF969DA7;
    private static final int COLOR_LINE = 0xFF2B323B;

    private final ExecutorService ioExecutor = Executors.newSingleThreadExecutor();

    private TextView discName;
    private TextView discStatus;
    private Button selectDiscButton;

    private Uri selectedDiscUri;
    private boolean selectedDiscValid;
    private String selectedDiscSerial;

    // The native runtime reads this descriptor through /proc/self/fd/<n>.
    // Keep it open while the launcher and NativeActivity share the same process.
    private static ParcelFileDescriptor selectedDiscDescriptor;

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
        root.setBackgroundColor(COLOR_BACKGROUND);

        LinearLayout shell = new LinearLayout(this);
        shell.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(shell, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        shell.addView(createSidebar(), new LinearLayout.LayoutParams(
                dp(150), ViewGroup.LayoutParams.MATCH_PARENT));

        View divider = new View(this);
        divider.setBackgroundColor(COLOR_LINE);
        shell.addView(divider, new LinearLayout.LayoutParams(
                dp(1), ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout main = new LinearLayout(this);
        main.setOrientation(LinearLayout.VERTICAL);
        main.setBackgroundColor(COLOR_BACKGROUND);
        shell.addView(main, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        main.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(22), dp(17), dp(22), dp(14));
        scroll.addView(content, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        content.addView(createPageHeader(), matchWrap());
        content.addView(createHeroCard(), topMargin(17));

        TextView actionsLabel = sectionLabel("AÇÕES");
        content.addView(actionsLabel, topMargin(17));

        LinearLayout actionRow = new LinearLayout(this);
        actionRow.setOrientation(LinearLayout.HORIZONTAL);

        Button playButton = actionButton("▶   JOGAR", true);
        playButton.setOnClickListener(view -> onPlayPressed());
        actionRow.addView(playButton, new LinearLayout.LayoutParams(
                0, dp(47), 0.72f));

        View gap = new View(this);
        actionRow.addView(gap, new LinearLayout.LayoutParams(dp(9), 1));

        selectDiscButton = actionButton("＋   SELECIONAR IMAGEM DO DISCO", false);
        selectDiscButton.setOnClickListener(
                view -> openDocumentLauncher.launch(new String[]{"*/*"}));
        actionRow.addView(selectDiscButton, new LinearLayout.LayoutParams(
                0, dp(47), 1.28f));
        content.addView(actionRow, topMargin(8));

        content.addView(sectionLabel("STATUS"), topMargin(17));

        LinearLayout statusRow = new LinearLayout(this);
        statusRow.setOrientation(LinearLayout.HORIZONTAL);
        statusRow.addView(createDiscStatusCard(), new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.2f));

        View statusGap = new View(this);
        statusRow.addView(statusGap, new LinearLayout.LayoutParams(dp(9), 1));

        statusRow.addView(createRuntimeStatusCard(), new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        content.addView(statusRow, topMargin(8));

        TextView footer = text(
                "FORGE  •  ANDROID ARM64  •  PORT NATIVO",
                8, COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        footer.setGravity(Gravity.CENTER);
        footer.setLetterSpacing(0.07f);
        content.addView(footer, topMargin(15));

        setContentView(root);
    }

    private View createSidebar() {
        LinearLayout sidebar = new LinearLayout(this);
        sidebar.setOrientation(LinearLayout.VERTICAL);
        sidebar.setPadding(dp(14), dp(18), dp(12), dp(15));
        sidebar.setBackgroundColor(COLOR_SIDEBAR);

        LinearLayout brand = new LinearLayout(this);
        brand.setOrientation(LinearLayout.HORIZONTAL);
        brand.setGravity(Gravity.CENTER_VERTICAL);

        FrameLayout logoPlate = new FrameLayout(this);
        logoPlate.setBackgroundColor(COLOR_RED);
        ImageView omega = new ImageView(this);
        omega.setImageResource(R.drawable.ic_launcher_emblem);
        omega.setScaleType(ImageView.ScaleType.FIT_CENTER);
        omega.setContentDescription("Ômega branco sobre fundo vermelho");
        FrameLayout.LayoutParams omegaParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT);
        omegaParams.setMargins(dp(5), dp(5), dp(5), dp(5));
        logoPlate.addView(omega, omegaParams);
        brand.addView(logoPlate, new LinearLayout.LayoutParams(dp(38), dp(38)));

        LinearLayout brandText = new LinearLayout(this);
        brandText.setOrientation(LinearLayout.VERTICAL);
        brandText.setPadding(dp(9), 0, 0, 0);

        TextView brandTitle = text(
                "FORGE", 15, COLOR_TEXT,
                Typeface.create("sans-serif", Typeface.BOLD));
        brandTitle.setLetterSpacing(0.12f);
        brandText.addView(brandTitle, matchWrap());

        TextView brandSubtitle = text(
                "GAME LIBRARY", 7, COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        brandSubtitle.setLetterSpacing(0.10f);
        LinearLayout.LayoutParams subtitleParams = matchWrap();
        subtitleParams.topMargin = dp(2);
        brandText.addView(brandSubtitle, subtitleParams);
        brand.addView(brandText, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        sidebar.addView(brand, matchWrap());

        sidebar.addView(createDivider(), topMargin(20));

        TextView libraryLabel = text(
                "LIBRARY", 8, COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        libraryLabel.setLetterSpacing(0.14f);
        sidebar.addView(libraryLabel, topMargin(21));

        TextView selectedItem = text(
                "▣   BIBLIOTECA", 10, COLOR_TEXT,
                Typeface.create("sans-serif-medium", Typeface.BOLD));
        selectedItem.setGravity(Gravity.CENTER_VERTICAL);
        selectedItem.setPadding(dp(10), 0, dp(5), 0);
        selectedItem.setBackground(rounded(COLOR_SURFACE_RAISED, COLOR_RED, dp(3)));
        LinearLayout.LayoutParams selectedParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(39));
        selectedParams.topMargin = dp(9);
        sidebar.addView(selectedItem, selectedParams);

        Button settingsButton = sidebarButton("⚙   CONFIGURAÇÕES");
        settingsButton.setOnClickListener(view -> showSettingsDialog());
        sidebar.addView(settingsButton, topMargin(6));

        Button aboutButton = sidebarButton("ⓘ   SOBRE");
        aboutButton.setOnClickListener(view -> showAboutDialog());
        sidebar.addView(aboutButton, topMargin(6));

        View spacer = new View(this);
        sidebar.addView(spacer, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        sidebar.addView(createDivider(), topMargin(10));

        TextView versionLabel = text(
                "VERSÃO", 7, COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        versionLabel.setLetterSpacing(0.13f);
        sidebar.addView(versionLabel, topMargin(11));

        TextView version = text(
                "0.1.0-dev", 9, COLOR_TEXT,
                Typeface.create("monospace", Typeface.NORMAL));
        sidebar.addView(version, topMargin(4));

        Button exitButton = sidebarButton("⇥   SAIR");
        exitButton.setOnClickListener(view -> finish());
        sidebar.addView(exitButton, topMargin(9));

        return sidebar;
    }

    private View createPageHeader() {
        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);

        LinearLayout labels = new LinearLayout(this);
        labels.setOrientation(LinearLayout.VERTICAL);

        TextView eyebrow = text(
                "FORGE  /  GOW_ANDROID", 8, COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        eyebrow.setLetterSpacing(0.13f);
        labels.addView(eyebrow, matchWrap());

        TextView title = text(
                "Biblioteca", 21, COLOR_TEXT,
                Typeface.create("sans-serif", Typeface.BOLD));
        LinearLayout.LayoutParams titleParams = matchWrap();
        titleParams.topMargin = dp(3);
        labels.addView(title, titleParams);

        header.addView(labels, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        TextView platform = text(
                "ANDROID  /  ARM64", 8, COLOR_MUTED,
                Typeface.create("monospace", Typeface.NORMAL));
        platform.setGravity(Gravity.CENTER);
        platform.setPadding(dp(10), dp(7), dp(10), dp(7));
        platform.setBackground(rounded(COLOR_SURFACE, COLOR_LINE, dp(3)));
        header.addView(platform, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(29)));

        return header;
    }

    private View createHeroCard() {
        FrameLayout card = new FrameLayout(this);
        card.setBackground(rounded(COLOR_SURFACE, COLOR_LINE, dp(5)));

        View accent = new View(this);
        accent.setBackgroundColor(COLOR_RED);
        FrameLayout.LayoutParams accentParams = new FrameLayout.LayoutParams(
                dp(3), ViewGroup.LayoutParams.MATCH_PARENT, Gravity.START);
        card.addView(accent, accentParams);

        ImageView watermark = new ImageView(this);
        watermark.setImageResource(R.drawable.ic_launcher_emblem);
        watermark.setScaleType(ImageView.ScaleType.FIT_CENTER);
        watermark.setAlpha(0.10f);
        FrameLayout.LayoutParams watermarkParams = new FrameLayout.LayoutParams(
                dp(142), dp(142), Gravity.END | Gravity.CENTER_VERTICAL);
        watermarkParams.rightMargin = dp(12);
        card.addView(watermark, watermarkParams);

        LinearLayout details = new LinearLayout(this);
        details.setOrientation(LinearLayout.VERTICAL);
        details.setGravity(Gravity.CENTER_VERTICAL);
        details.setPadding(dp(19), dp(15), dp(10), dp(15));

        TextView eyebrow = text(
                "PLAYSTATION 2  •  2005", 8, COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        eyebrow.setLetterSpacing(0.12f);
        details.addView(eyebrow, matchWrap());

        TextView gameTitle = text(
                "God of War", 28, COLOR_TEXT,
                Typeface.create("sans-serif", Typeface.BOLD));
        LinearLayout.LayoutParams titleParams = matchWrap();
        titleParams.topMargin = dp(5);
        details.addView(gameTitle, titleParams);

        TextView gameDescription = text(
                "Port nativo em desenvolvimento para Android",
                10, COLOR_MUTED,
                Typeface.create("sans-serif", Typeface.NORMAL));
        LinearLayout.LayoutParams descriptionParams = matchWrap();
        descriptionParams.topMargin = dp(4);
        details.addView(gameDescription, descriptionParams);

        TextView badge = text(
                "EM DESENVOLVIMENTO", 7, 0xFFFFA5A8,
                Typeface.create("sans-serif-medium", Typeface.BOLD));
        badge.setLetterSpacing(0.09f);
        badge.setPadding(dp(8), dp(5), dp(8), dp(5));
        badge.setBackground(rounded(0x332E1418, 0x665B252C, dp(2)));
        LinearLayout.LayoutParams badgeParams = matchWrap();
        badgeParams.topMargin = dp(10);
        details.addView(badge, badgeParams);

        FrameLayout.LayoutParams detailsParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT);
        detailsParams.rightMargin = dp(100);
        card.addView(details, detailsParams);

        card.setMinimumHeight(dp(151));
        return card;
    }

    private View createDiscStatusCard() {
        LinearLayout card = statusCard();
        TextView label = sectionLabel("IMAGEM DO DISCO");
        card.addView(label, matchWrap());

        discName = text(
                "Nenhuma imagem selecionada", 11, COLOR_TEXT,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        LinearLayout.LayoutParams nameParams = matchWrap();
        nameParams.topMargin = dp(7);
        card.addView(discName, nameParams);

        discStatus = text(
                "Selecione sua cópia do jogo para verificar o formato.",
                9, COLOR_MUTED,
                Typeface.create("sans-serif", Typeface.NORMAL));
        discStatus.setLineSpacing(dp(2), 1f);
        LinearLayout.LayoutParams statusParams = matchWrap();
        statusParams.topMargin = dp(4);
        card.addView(discStatus, statusParams);
        return card;
    }

    private View createRuntimeStatusCard() {
        LinearLayout card = statusCard();
        card.addView(sectionLabel("ESTADO DO PORT"), matchWrap());

        TextView value = text(
                "Em desenvolvimento", 11, COLOR_TEXT,
                Typeface.create("sans-serif-medium", Typeface.BOLD));
        LinearLayout.LayoutParams valueParams = matchWrap();
        valueParams.topMargin = dp(7);
        card.addView(value, valueParams);

        TextView detail = text(
                "FORGE já abre em aparelho Android. Runtime nativo do jogo em integração.",
                9, COLOR_MUTED,
                Typeface.create("sans-serif", Typeface.NORMAL));
        detail.setLineSpacing(dp(2), 1f);
        LinearLayout.LayoutParams detailParams = matchWrap();
        detailParams.topMargin = dp(4);
        card.addView(detail, detailParams);
        return card;
    }

    private LinearLayout statusCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(13), dp(12), dp(13), dp(12));
        card.setBackground(rounded(COLOR_SURFACE, COLOR_LINE, dp(4)));
        return card;
    }

    private View createDivider() {
        View divider = new View(this);
        divider.setBackgroundColor(COLOR_LINE);
        return divider;
    }

    private TextView sectionLabel(String value) {
        TextView label = text(
                value, 8, COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        label.setLetterSpacing(0.13f);
        return label;
    }

    private Button actionButton(String label, boolean primary) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setGravity(Gravity.CENTER_VERTICAL | Gravity.START);
        button.setTextSize(10);
        button.setLetterSpacing(0.02f);
        button.setTypeface(Typeface.create(
                "sans-serif-medium", primary ? Typeface.BOLD : Typeface.NORMAL));
        button.setTextColor(primary ? Color.WHITE : COLOR_TEXT);
        button.setPadding(dp(12), 0, dp(8), 0);
        button.setMinHeight(dp(44));
        button.setStateListAnimator(null);
        button.setBackground(actionBackground(primary, false));
        button.setOnFocusChangeListener((view, focused) ->
                button.setBackground(actionBackground(primary, focused)));
        return button;
    }

    private Button sidebarButton(String label) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setGravity(Gravity.CENTER_VERTICAL | Gravity.START);
        button.setTextSize(9);
        button.setLetterSpacing(0.01f);
        button.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        button.setTextColor(COLOR_MUTED);
        button.setPadding(dp(8), 0, dp(2), 0);
        button.setMinHeight(dp(38));
        button.setStateListAnimator(null);
        button.setBackground(rounded(COLOR_SIDEBAR, Color.TRANSPARENT, dp(3)));
        return button;
    }

    private GradientDrawable actionBackground(boolean primary, boolean focused) {
        if (primary) {
            return rounded(focused ? COLOR_RED_BRIGHT : COLOR_RED,
                    focused ? 0xFFFF777D : COLOR_RED, dp(4));
        }
        return rounded(focused ? COLOR_SURFACE_RAISED : COLOR_SURFACE,
                focused ? COLOR_MUTED : COLOR_LINE, dp(4));
    }

    private GradientDrawable rounded(int fill, int stroke, int radius) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(fill);
        drawable.setCornerRadius(radius);
        if (stroke != Color.TRANSPARENT) {
            drawable.setStroke(dp(1), stroke);
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
                ? "Disco identificado: " + selectedDiscSerial + ".\\n\\n"
                        + "O FORGE já consegue confirmar a identidade do disco por meio do "
                        + "SYSTEM.CNF. O runtime nativo ainda está em integração; o jogo só será "
                        + "iniciado quando a recompilação estática e o boot estiverem prontos."
                : "A imagem ainda não passou pela identificação ISO9660/SYSTEM.CNF. "
                        + "Selecione uma imagem compatível de God of War (2005).";

        new AlertDialog.Builder(this)
                .setTitle(selectedDiscValid ? "Disco reconhecido" : "Imagem não validada")
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
                                + "GRÁFICOS
Vulkan (renderizador do jogo em desenvolvimento)

"
                                + "INTERFACE\nFORGE\n\n"
                                + "VERSÃO\n0.1.0-dev\n\n"
                                + "As opções de resolução, controles e áudio serão adicionadas "
                                + "quando os respectivos subsistemas estiverem integrados.")
                .setPositiveButton("Fechar", null)
                .show();
    }

    private void showAboutDialog() {
        new AlertDialog.Builder(this)
                .setTitle("Sobre o FORGE")
                .setMessage(
                        "FORGE é a interface de biblioteca do projeto Gow_android, que busca "
                                + "construir um port nativo de God of War (2005) para Android.\n\n"
                                + "A interface e a seleção de disco são etapas iniciais. O runtime "
                                + "nativo e o renderizador do jogo ainda estão em desenvolvimento. "
                                + "A validação gráfica será feita durante a execução do jogo.

"
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
            selectDiscButton.setAlpha(0.55f);
        }
        discName.setText("Verificando imagem...");
        discStatus.setText("Lendo ISO9660 e identificando SYSTEM.CNF.");
        discStatus.setTextColor(COLOR_MUTED);
        selectedDiscValid = false;
        selectedDiscSerial = null;

        ioExecutor.execute(() -> {
            String name = queryDisplayName(uri);
            String result;
            boolean valid = false;
            ParcelFileDescriptor candidate = null;
            String serial = null;

            try {
                candidate = getContentResolver().openFileDescriptor(uri, "r");
                if (candidate == null) {
                    throw new IOException("O provedor não conseguiu abrir o arquivo.");
                }

                serial = identifyGodOfWarDisc(candidate);
                ParcelFileDescriptor previous = selectedDiscDescriptor;
                selectedDiscDescriptor = candidate;
                candidate = null;
                closeQuietly(previous);

                valid = true;
                result = "God of War (2005) reconhecido · " + serial;
            } catch (IOException | SecurityException | ErrnoException exception) {
                result = "Falha na identificação: " + safeMessage(exception);
            } finally {
                closeQuietly(candidate);
            }

            final boolean isValid = valid;
            final String finalName = name;
            final String finalResult = result;
            final String finalSerial = serial;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) {
                    return;
                }
                selectedDiscValid = isValid;
                selectedDiscSerial = finalSerial;
                discName.setText(finalName);
                discStatus.setText(finalResult);
                discStatus.setTextColor(
                        isValid ? 0xFF8BC28F : 0xFFE27676);
                if (selectDiscButton != null) {
                    selectDiscButton.setEnabled(true);
                    selectDiscButton.setAlpha(1f);
                }
            });
        });
    }

    /**
     * Validates an ISO9660 primary volume descriptor and reads the root
     * SYSTEM.CNF file to identify the actual God of War executable.
     * This intentionally supports 2048-byte ISO sectors; raw 2352-byte BIN
     * and compressed CHD images require a separate disc-image reader.
     */
    private String identifyGodOfWarDisc(ParcelFileDescriptor descriptor)
            throws IOException, ErrnoException {
        byte[] pvd = readAt(descriptor, PRIMARY_VOLUME_DESCRIPTOR_OFFSET, ISO_SECTOR_SIZE);
        if ((pvd[0] & 0xFF) != 1
                || pvd[1] != 'C'
                || pvd[2] != 'D'
                || pvd[3] != '0'
                || pvd[4] != '0'
                || pvd[5] != '1'
                || (pvd[6] & 0xFF) != 1) {
            throw new IOException(
                    "O setor 16 não contém um descritor primário ISO9660 válido.");
        }

        int rootRecordLength = pvd[156] & 0xFF;
        if (rootRecordLength < 34) {
            throw new IOException("O descritor ISO9660 não contém um diretório raiz válido.");
        }

        long rootExtent = readUInt32Le(pvd, 158);
        long rootSize = readUInt32Le(pvd, 166);
        if (rootSize <= 0 || rootSize > 16L * 1024L * 1024L) {
            throw new IOException("Tamanho do diretório raiz ISO9660 inválido.");
        }

        byte[] rootDirectory = readAt(
                descriptor, rootExtent * ISO_SECTOR_SIZE, (int) rootSize);
        long systemCnfExtent = -1L;
        long systemCnfSize = -1L;

        int offset = 0;
        while (offset < rootDirectory.length) {
            int recordLength = rootDirectory[offset] & 0xFF;
            if (recordLength == 0) {
                offset = ((offset / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
                continue;
            }
            if (recordLength < 34 || offset + recordLength > rootDirectory.length) {
                throw new IOException("Registro de diretório ISO9660 inválido.");
            }

            int identifierLength = rootDirectory[offset + 32] & 0xFF;
            if (identifierLength > 0
                    && offset + 33 + identifierLength <= offset + recordLength) {
                String identifier = new String(
                        rootDirectory, offset + 33, identifierLength,
                        StandardCharsets.US_ASCII).toUpperCase(Locale.ROOT);
                if ("SYSTEM.CNF".equals(identifier)
                        || identifier.startsWith("SYSTEM.CNF;")) {
                    systemCnfExtent = readUInt32Le(rootDirectory, offset + 2);
                    systemCnfSize = readUInt32Le(rootDirectory, offset + 10);
                    break;
                }
            }
            offset += recordLength;
        }

        if (systemCnfExtent < 0 || systemCnfSize <= 0 || systemCnfSize > 64L * 1024L) {
            throw new IOException("SYSTEM.CNF não foi encontrado na raiz do disco.");
        }

        byte[] systemCnf = readAt(
                descriptor, systemCnfExtent * ISO_SECTOR_SIZE, (int) systemCnfSize);
        String text = new String(systemCnf, StandardCharsets.US_ASCII);
        String bootTarget = null;

        for (String line : text.split("\\r?\\n")) {
            int equals = line.indexOf('=');
            if (equals < 0) {
                continue;
            }
            String key = line.substring(0, equals).trim();
            if ("BOOT2".equalsIgnoreCase(key)) {
                bootTarget = line.substring(equals + 1).trim();
                break;
            }
        }

        if (bootTarget == null || bootTarget.isEmpty()) {
            throw new IOException("SYSTEM.CNF não contém a entrada BOOT2.");
        }

        String executablePath = bootTarget.replace('\\', '/');
        int colon = executablePath.indexOf(':');
        if (colon >= 0) {
            executablePath = executablePath.substring(colon + 1);
        }
        int slash = executablePath.lastIndexOf('/');
        String executable = slash >= 0
                ? executablePath.substring(slash + 1)
                : executablePath;
        int version = executable.indexOf(';');
        if (version >= 0) {
            executable = executable.substring(0, version);
        }
        String serial = executable.trim().toUpperCase(Locale.ROOT);

        if ("SCUS_973.99".equals(serial)) {
            return "SCUS_973.99 (NTSC-U)";
        }
        if ("SCES_531.33".equals(serial)) {
            return "SCES_531.33 (PAL)";
        }
        throw new IOException(
                "SYSTEM.CNF aponta para " + serial
                        + ". Esta versão reconhece SCUS_973.99 (NTSC-U) e SCES_531.33 (PAL).");
    }

    private static byte[] readAt(
            ParcelFileDescriptor descriptor, long offset, int length)
            throws IOException, ErrnoException {
        byte[] buffer = new byte[length];
        int total = 0;
        while (total < length) {
            int count = Os.pread(
                    descriptor.getFileDescriptor(),
                    buffer,
                    total,
                    length - total,
                    offset + total);
            if (count <= 0) {
                throw new IOException("Fim inesperado ao ler a imagem ISO9660.");
            }
            total += count;
        }
        return buffer;
    }

    private static long readUInt32Le(byte[] data, int offset) throws IOException {
        if (offset < 0 || offset + 4 > data.length) {
            throw new IOException("Campo ISO9660 fora dos limites.");
        }
        return (data[offset] & 0xFFL)
                | ((data[offset + 1] & 0xFFL) << 8)
                | ((data[offset + 2] & 0xFFL) << 16)
                | ((data[offset + 3] & 0xFFL) << 24);
    }

    private static void closeQuietly(ParcelFileDescriptor descriptor) {
        if (descriptor == null) {
            return;
        }
        try {
            descriptor.close();
        } catch (IOException ignored) {
            // The descriptor may already have been closed by Android.
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

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    protected void onDestroy() {
        ioExecutor.shutdownNow();
        super.onDestroy();
    }
}

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
 * Steam-inspired library launcher with an original Greek/ancient-war visual theme.
 *
 * The interface contains no character artwork from the original game. It selects
 * a user-owned disc image and validates its ISO9660 primary volume descriptor;
 * it does not yet identify SYSTEM.CNF or boot the game.
 */
public final class MainActivity extends AppCompatActivity {
    private static final String PREF_DISC_URI = "selected_disc_uri";
    private static final int ISO_SECTOR_SIZE = 2048;
    private static final long PRIMARY_VOLUME_DESCRIPTOR_OFFSET = 16L * ISO_SECTOR_SIZE;

    private static final int COLOR_BACKGROUND = 0xFF090708;
    private static final int COLOR_PANEL = 0xFF171011;
    private static final int COLOR_RED = 0xFF9D2925;
    private static final int COLOR_RED_BRIGHT = 0xFFD34A36;
    private static final int COLOR_BRONZE = 0xFFC09A67;
    private static final int COLOR_TEXT = 0xFFE9DECB;
    private static final int COLOR_MUTED = 0xFF9D9185;
    private static final int COLOR_LINE = 0xFF60462F;

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
                new int[]{0xFF090708, 0xFF160C0D, 0xFF0A0808},
                GradientDrawable.Orientation.TL_BR,
                0,
                0));

        LinearLayout shell = new LinearLayout(this);
        shell.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(shell, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        shell.addView(createSidebar(), new LinearLayout.LayoutParams(
                dp(166),
                ViewGroup.LayoutParams.MATCH_PARENT));

        View sidebarEdge = new View(this);
        sidebarEdge.setBackground(new GradientDrawable(
                GradientDrawable.Orientation.TOP_BOTTOM,
                new int[]{0xFF8E3028, 0xFF5B3828, 0xFF211514}));
        shell.addView(sidebarEdge, new LinearLayout.LayoutParams(dp(2),
                ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout main = new LinearLayout(this);
        main.setOrientation(LinearLayout.VERTICAL);
        main.setBackgroundColor(0x66090607);
        shell.addView(main, new LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.MATCH_PARENT,
                1f));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        main.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(20), dp(15), dp(20), dp(14));
        scroll.addView(content, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        content.addView(createPageHeading(), matchWrap());
        content.addView(createDivider(), topMargin(10));
        content.addView(createHeroCard(), topMargin(12));

        TextView actionsLabel = sectionLabel("AÇÕES RÁPIDAS");
        content.addView(actionsLabel, topMargin(13));

        LinearLayout primaryActions = new LinearLayout(this);
        primaryActions.setOrientation(LinearLayout.HORIZONTAL);

        Button playButton = menuButton("▶   JOGAR", true);
        playButton.setOnClickListener(view -> onPlayPressed());
        primaryActions.addView(playButton, new LinearLayout.LayoutParams(
                0, dp(48), 0.78f));

        View actionGap = new View(this);
        primaryActions.addView(actionGap, new LinearLayout.LayoutParams(dp(8), 1));

        selectDiscButton = menuButton("＋   SELECIONAR IMAGEM DO DISCO", false);
        selectDiscButton.setOnClickListener(
                view -> openDocumentLauncher.launch(new String[]{"*/*"}));
        primaryActions.addView(selectDiscButton, new LinearLayout.LayoutParams(
                0, dp(48), 1.42f));
        content.addView(primaryActions, topMargin(7));

        Button vulkanButton = menuButton("◇   TESTAR APRESENTAÇÃO VULKAN", false);
        vulkanButton.setOnClickListener(
                view -> startActivity(new Intent(this, GowGameActivity.class)));
        content.addView(vulkanButton, topMargin(7));

        content.addView(sectionLabel("DETALHES DA BIBLIOTECA"), topMargin(13));

        LinearLayout statusRow = new LinearLayout(this);
        statusRow.setOrientation(LinearLayout.HORIZONTAL);
        statusRow.addView(createDiscStatusCard(), new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.45f));

        View statusGap = new View(this);
        statusRow.addView(statusGap, new LinearLayout.LayoutParams(dp(8), 1));

        statusRow.addView(createRuntimeStatusCard(), new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        content.addView(statusRow, topMargin(7));

        TextView footer = text(
                "ANDROID ARM64   •   VULKAN   •   PORT NATIVO EM DESENVOLVIMENTO",
                8,
                0xFF81756B,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        footer.setGravity(Gravity.CENTER);
        footer.setLetterSpacing(0.06f);
        content.addView(footer, topMargin(12));

        View topAccent = new View(this);
        topAccent.setBackground(new GradientDrawable(
                GradientDrawable.Orientation.LEFT_RIGHT,
                new int[]{COLOR_RED, COLOR_BRONZE, 0x0060402F}));
        root.addView(topAccent, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(2),
                Gravity.TOP));

        setContentView(root);
    }

    private View createSidebar() {
        LinearLayout sidebar = new LinearLayout(this);
        sidebar.setOrientation(LinearLayout.VERTICAL);
        sidebar.setPadding(dp(13), dp(17), dp(12), dp(14));
        sidebar.setBackground(gradient(
                new int[]{0xFF171011, 0xFF0D0A0B},
                GradientDrawable.Orientation.TOP_BOTTOM,
                0,
                0));

        LinearLayout brand = new LinearLayout(this);
        brand.setOrientation(LinearLayout.HORIZONTAL);
        brand.setGravity(Gravity.CENTER_VERTICAL);

        ImageView emblem = new ImageView(this);
        emblem.setImageResource(R.drawable.ic_launcher_emblem);
        emblem.setScaleType(ImageView.ScaleType.FIT_CENTER);
        emblem.setContentDescription("Emblema grego original do projeto");
        brand.addView(emblem, new LinearLayout.LayoutParams(dp(43), dp(43)));

        LinearLayout brandText = new LinearLayout(this);
        brandText.setOrientation(LinearLayout.VERTICAL);
        brandText.setPadding(dp(8), 0, 0, 0);

        TextView brandTitle = text(
                "GOW_ANDROID",
                11,
                COLOR_TEXT,
                Typeface.create("serif", Typeface.BOLD));
        brandTitle.setLetterSpacing(0.02f);
        brandText.addView(brandTitle, matchWrap());

        TextView brandSubtitle = text(
                "ANDROID NATIVE PORT",
                7,
                COLOR_BRONZE,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        brandSubtitle.setLetterSpacing(0.10f);
        LinearLayout.LayoutParams subtitleParams = matchWrap();
        subtitleParams.topMargin = dp(3);
        brandText.addView(brandSubtitle, subtitleParams);

        brand.addView(brandText, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        sidebar.addView(brand, matchWrap());
        sidebar.addView(createDivider(), topMargin(15));

        TextView collectionLabel = text(
                "SUA COLEÇÃO",
                8,
                COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        collectionLabel.setLetterSpacing(0.14f);
        sidebar.addView(collectionLabel, topMargin(19));

        TextView libraryItem = text(
                "▣   BIBLIOTECA",
                11,
                COLOR_TEXT,
                Typeface.create("sans-serif-medium", Typeface.BOLD));
        libraryItem.setGravity(Gravity.CENTER_VERTICAL);
        libraryItem.setPadding(dp(9), 0, dp(4), 0);
        libraryItem.setBackground(gradient(
                new int[]{0xFF64201D, 0xFF321315},
                GradientDrawable.Orientation.LEFT_RIGHT,
                dp(1),
                0xFF9D3B31));
        sidebar.addView(libraryItem, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(42)));

        Button settingsButton = sidebarButton("⚙   CONFIGURAÇÕES");
        settingsButton.setOnClickListener(view -> showSettingsDialog());
        sidebar.addView(settingsButton, topMargin(7));

        Button aboutButton = sidebarButton("ⓘ   SOBRE");
        aboutButton.setOnClickListener(view -> showAboutDialog());
        sidebar.addView(aboutButton, topMargin(7));

        View spacer = new View(this);
        sidebar.addView(spacer, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        sidebar.addView(createDivider(), topMargin(8));

        TextView buildLabel = text(
                "VERSÃO DE DESENVOLVIMENTO",
                7,
                COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        buildLabel.setLetterSpacing(0.08f);
        sidebar.addView(buildLabel, topMargin(10));

        TextView buildValue = text(
                "0.1.0-dev  •  ARM64",
                8,
                COLOR_BRONZE,
                Typeface.create("monospace", Typeface.NORMAL));
        sidebar.addView(buildValue, topMargin(4));

        Button exitButton = sidebarButton("⇥   SAIR");
        exitButton.setOnClickListener(view -> finish());
        sidebar.addView(exitButton, topMargin(11));

        return sidebar;
    }

    private View createPageHeading() {
        LinearLayout heading = new LinearLayout(this);
        heading.setOrientation(LinearLayout.HORIZONTAL);
        heading.setGravity(Gravity.CENTER_VERTICAL);

        LinearLayout labels = new LinearLayout(this);
        labels.setOrientation(LinearLayout.VERTICAL);

        TextView eyebrow = text(
                "GOW_ANDROID  /  COLEÇÃO",
                8,
                COLOR_BRONZE,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        eyebrow.setLetterSpacing(0.12f);
        labels.addView(eyebrow, matchWrap());

        TextView title = text(
                "BIBLIOTECA",
                18,
                COLOR_TEXT,
                Typeface.create("serif", Typeface.BOLD));
        title.setLetterSpacing(0.04f);
        LinearLayout.LayoutParams titleParams = matchWrap();
        titleParams.topMargin = dp(3);
        labels.addView(title, titleParams);
        heading.addView(labels, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        TextView version = text(
                "v0.1.0 DEV",
                8,
                COLOR_BRONZE,
                Typeface.create("monospace", Typeface.NORMAL));
        version.setGravity(Gravity.CENTER);
        version.setPadding(dp(9), dp(6), dp(9), dp(6));
        version.setBackground(gradient(
                new int[]{0xFF241718, 0xFF171011},
                GradientDrawable.Orientation.TOP_BOTTOM,
                dp(1),
                COLOR_LINE));
        heading.addView(version, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(28)));

        return heading;
    }

    private View createHeroCard() {
        FrameLayout hero = new FrameLayout(this);
        hero.setBackground(gradient(
                new int[]{0xFF421718, 0xFF211011, 0xFF100C0C},
                GradientDrawable.Orientation.LEFT_RIGHT,
                dp(1),
                0xFF8A5739));

        ImageView sigil = new ImageView(this);
        sigil.setImageResource(R.drawable.ic_launcher_emblem);
        sigil.setScaleType(ImageView.ScaleType.FIT_CENTER);
        sigil.setAlpha(0.22f);
        FrameLayout.LayoutParams sigilParams = new FrameLayout.LayoutParams(
                dp(156),
                dp(156),
                Gravity.END | Gravity.CENTER_VERTICAL);
        sigilParams.rightMargin = dp(8);
        hero.addView(sigil, sigilParams);

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setGravity(Gravity.CENTER_VERTICAL);
        copy.setPadding(dp(18), dp(12), dp(12), dp(12));

        TextView era = text(
                "PLAYSTATION 2   •   2005",
                8,
                COLOR_BRONZE,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        era.setLetterSpacing(0.13f);
        copy.addView(era, matchWrap());

        TextView title = text(
                "GOD OF WAR",
                29,
                COLOR_TEXT,
                Typeface.create("serif", Typeface.BOLD));
        title.setLetterSpacing(0.03f);
        LinearLayout.LayoutParams titleParams = matchWrap();
        titleParams.topMargin = dp(4);
        copy.addView(title, titleParams);

        TextView subtitle = text(
                "ANDROID NATIVE PORT",
                9,
                COLOR_BRONZE,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        subtitle.setLetterSpacing(0.20f);
        LinearLayout.LayoutParams subtitleParams = matchWrap();
        subtitleParams.topMargin = dp(1);
        copy.addView(subtitle, subtitleParams);

        View redRule = new View(this);
        redRule.setBackgroundColor(COLOR_RED);
        LinearLayout.LayoutParams ruleParams = new LinearLayout.LayoutParams(
                dp(104), dp(2));
        ruleParams.topMargin = dp(10);
        copy.addView(redRule, ruleParams);

        TextView tagline = text(
                "A VINGANÇA DE UM DEUS COMEÇA AQUI.",
                9,
                COLOR_TEXT,
                Typeface.create("serif", Typeface.BOLD));
        tagline.setLetterSpacing(0.08f);
        LinearLayout.LayoutParams taglineParams = matchWrap();
        taglineParams.topMargin = dp(8);
        copy.addView(tagline, taglineParams);

        FrameLayout.LayoutParams copyParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT);
        copyParams.rightMargin = dp(105);
        hero.addView(copy, copyParams);

        return hero;
    }

    private View createDiscStatusCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(12), dp(10), dp(12), dp(10));
        card.setBackground(gradient(
                new int[]{0xE51A1313, 0xE50F0C0C},
                GradientDrawable.Orientation.TOP_BOTTOM,
                dp(1),
                0xFF654A34));

        TextView label = text(
                "IMAGEM DO DISCO",
                8,
                COLOR_BRONZE,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        label.setLetterSpacing(0.12f);
        card.addView(label, matchWrap());

        discName = text(
                "Nenhuma imagem selecionada",
                11,
                COLOR_TEXT,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        LinearLayout.LayoutParams nameParams = matchWrap();
        nameParams.topMargin = dp(4);
        card.addView(discName, nameParams);

        discStatus = text(
                "Adicione sua cópia do jogo para verificar o formato.",
                9,
                COLOR_MUTED,
                Typeface.create("sans-serif", Typeface.NORMAL));
        discStatus.setLineSpacing(dp(2), 1f);
        LinearLayout.LayoutParams statusParams = matchWrap();
        statusParams.topMargin = dp(3);
        card.addView(discStatus, statusParams);

        return card;
    }

    private View createRuntimeStatusCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(12), dp(10), dp(10), dp(10));
        card.setBackground(gradient(
                new int[]{0xE51A1313, 0xE50F0C0C},
                GradientDrawable.Orientation.TOP_BOTTOM,
                dp(1),
                0xFF654A34));

        TextView label = text(
                "ESTADO DO PORT",
                8,
                COLOR_BRONZE,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        label.setLetterSpacing(0.12f);
        card.addView(label, matchWrap());

        TextView value = text(
                "EM DESENVOLVIMENTO",
                10,
                COLOR_TEXT,
                Typeface.create("sans-serif-medium", Typeface.BOLD));
        LinearLayout.LayoutParams valueParams = matchWrap();
        valueParams.topMargin = dp(5);
        card.addView(value, valueParams);

        TextView detail = text(
                "Launcher e teste Vulkan disponíveis. Runtime do jogo pendente.",
                9,
                COLOR_MUTED,
                Typeface.create("sans-serif", Typeface.NORMAL));
        detail.setLineSpacing(dp(2), 1f);
        LinearLayout.LayoutParams detailParams = matchWrap();
        detailParams.topMargin = dp(3);
        card.addView(detail, detailParams);

        return card;
    }

    private View createDivider() {
        View divider = new View(this);
        divider.setBackground(new GradientDrawable(
                GradientDrawable.Orientation.LEFT_RIGHT,
                new int[]{COLOR_RED, COLOR_BRONZE, 0x2260402F}));
        divider.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(2)));
        return divider;
    }

    private TextView sectionLabel(String value) {
        TextView label = text(
                value,
                8,
                COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        label.setLetterSpacing(0.14f);
        return label;
    }

    private Button menuButton(String label, boolean primary) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setGravity(Gravity.CENTER_VERTICAL | Gravity.START);
        button.setTextSize(10);
        button.setLetterSpacing(0.025f);
        button.setTypeface(Typeface.create(
                "serif", primary ? Typeface.BOLD : Typeface.NORMAL));
        button.setTextColor(primary ? Color.WHITE : COLOR_TEXT);
        button.setPadding(dp(12), 0, dp(8), 0);
        button.setMinHeight(dp(44));
        button.setStateListAnimator(null);
        button.setFocusable(true);
        button.setBackground(menuButtonBackground(primary, false));
        button.setOnFocusChangeListener((view, focused) ->
                button.setBackground(menuButtonBackground(primary, focused)));
        return button;
    }

    private Button sidebarButton(String label) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setGravity(Gravity.CENTER_VERTICAL | Gravity.START);
        button.setTextSize(9);
        button.setLetterSpacing(0.02f);
        button.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        button.setTextColor(COLOR_MUTED);
        button.setPadding(dp(8), 0, dp(2), 0);
        button.setMinHeight(dp(39));
        button.setStateListAnimator(null);
        button.setBackground(gradient(
                new int[]{0xFF1B1414, 0xFF120E0E},
                GradientDrawable.Orientation.LEFT_RIGHT,
                dp(1),
                0xFF392B25));
        return button;
    }

    private GradientDrawable menuButtonBackground(boolean primary, boolean focused) {
        int[] colors = primary || focused
                ? new int[]{0xFF9B2B25, 0xFF511719}
                : new int[]{0xFF302123, 0xFF181112};
        int stroke = primary || focused ? COLOR_RED_BRIGHT : 0xFF6B4C37;
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

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    protected void onDestroy() {
        ioExecutor.shutdownNow();
        super.onDestroy();
    }
}

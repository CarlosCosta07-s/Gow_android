package org.gowandroid.app;

import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.SystemClock;
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
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * FORGE is the minimalist game-library interface for the Gow_android project.
 *
 * It uses original UI elements only: no character portraits, screenshots or
 * extracted artwork from God of War. Disc selection, ISO9660 validation and
 * initial SYSTEM.CNF identification are implemented; game boot is not available yet.
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
    private static final int COLOR_SUCCESS = 0xFF8BC28F;
    private static final int COLOR_WARNING = 0xFFE0B04A;
    private static final int COLOR_ERROR = 0xFFE27676;

    private static final String FALLBACK_VERSION_NAME = "0.1.0-dev";

    private final ExecutorService ioExecutor = Executors.newSingleThreadExecutor();

    private interface InstallProgressCallback {
        boolean onProgress(long totalBytes, long copiedBytes,
                           int completedFiles, int totalFiles);
    }


    private static native String nativeInstallIsoDisc(
            int descriptor, String privateRoot, String finalDirectoryName,
            InstallProgressCallback progressCallback);
    private static native String nativeRemoveInstallationDirectory(
            String privateRoot, String installationDirectoryName);

    private static native boolean nativeSetCdRootDirectory(String privateCdRoot);
    private static native String nativeVerifyDiscIntegrity(
            String privateCdRoot, InstallProgressCallback progressCallback);
    private static native String nativeValidateCdRootElf(String relativePath);
    private static native String nativePrepareRuntimeIoPaths(String bootRelativePath);
    private static native String nativeValidateDiscSource(String bootRelativePath);
    private static native String nativeValidateIopCdvd(String bootRelativePath);

    static {
        System.loadLibrary("android-game");
    }

    private TextView discName;
    private TextView discStatus;
    private Button selectDiscButton;
    private Button installDiscButton;
    private AlertDialog discInstallDialog;
    private ProgressBar discInstallProgressBar;
    private TextView discInstallProgressText;

    private TextView clockTime;
    private TextView clockDate;
    private BatteryGaugeView batteryGauge;
    private TextView batteryPercent;
    private TextView batteryState;
    private DeviceStatusMonitor deviceStatusMonitor;
    private String cachedVersionName;

    private Uri selectedDiscUri;
    private boolean selectedDiscValid;
    private boolean installationReadyForBoot;
    private String selectedDiscSerial;
    private String selectedDiscBootPath;
    private boolean installingDisc;
    private volatile boolean cancelDiscInstallRequested;
    private String preparedDiscSummary;

    private final ActivityResultLauncher<String[]> openDocumentLauncher =
            registerForActivityResult(
                    new ActivityResultContracts.OpenDocument(),
                    this::onDiscSelected);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        applyImmersiveMode();
        buildLauncherUi();
        deviceStatusMonitor = new DeviceStatusMonitor(this,
                new DeviceStatusMonitor.Listener() {
                    @Override
                    public void onTimeChanged(long nowMillis) {
                        updateClock(nowMillis);
                    }

                    @Override
                    public void onBatteryChanged(BatteryReading reading) {
                        updateBattery(reading);
                    }
                });
        // Prefer the app-private installation over the original SAF URI.
        // After a native Activity/process crash, this keeps the installed disc
        // visible and prevents the launcher from incorrectly offering install again.
        if (!restorePrivateInstallationIfAvailable()) {
            restoreSelectedDisc();
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        // Refreshes the clock and battery immediately, including after the
        // launcher spent time in the background.
        if (deviceStatusMonitor != null) {
            deviceStatusMonitor.start();
        }
    }

    @Override
    protected void onStop() {
        if (deviceStatusMonitor != null) {
            deviceStatusMonitor.stop();
        }
        super.onStop();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // Dialogs and the system shade can drop immersive mode.
        if (hasFocus) {
            applyImmersiveMode();
        }
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

        installDiscButton = actionButton("INSTALAR JOGO", true);
        installDiscButton.setOnClickListener(view -> onPlayPressed());
        actionRow.addView(installDiscButton, new LinearLayout.LayoutParams(
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
                9, COLOR_MUTED,
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
                "VERSÃO", 8, COLOR_MUTED,
                Typeface.create("sans-serif-medium", Typeface.NORMAL));
        versionLabel.setLetterSpacing(0.13f);
        sidebar.addView(versionLabel, topMargin(11));

        TextView version = text(
                versionName(), 9, COLOR_TEXT,
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

        header.addView(createDeviceStatusCluster(), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        View clusterGap = new View(this);
        header.addView(clusterGap, new LinearLayout.LayoutParams(dp(8), 1));

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

    /** Clock and battery chip shown in the page header. */
    private View createDeviceStatusCluster() {
        LinearLayout cluster = new LinearLayout(this);
        cluster.setOrientation(LinearLayout.HORIZONTAL);
        cluster.setGravity(Gravity.CENTER_VERTICAL);
        cluster.setPadding(dp(10), dp(5), dp(10), dp(5));
        cluster.setBackground(rounded(COLOR_SURFACE, COLOR_LINE, dp(3)));

        LinearLayout clock = new LinearLayout(this);
        clock.setOrientation(LinearLayout.VERTICAL);

        clockTime = text("--:--", 13, COLOR_TEXT,
                Typeface.create("sans-serif-medium", Typeface.BOLD));
        clockTime.setSingleLine(true);
        clock.addView(clockTime, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        clockDate = text("", 8, COLOR_MUTED,
                Typeface.create("sans-serif", Typeface.NORMAL));
        clockDate.setSingleLine(true);
        clock.addView(clockDate, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        cluster.addView(clock, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        View separator = new View(this);
        separator.setBackgroundColor(COLOR_LINE);
        LinearLayout.LayoutParams separatorParams =
                new LinearLayout.LayoutParams(dp(1), dp(24));
        separatorParams.setMargins(dp(10), 0, dp(10), 0);
        cluster.addView(separator, separatorParams);

        batteryGauge = new BatteryGaugeView(this);
        cluster.addView(batteryGauge, new LinearLayout.LayoutParams(
                dp(26), dp(13)));

        LinearLayout battery = new LinearLayout(this);
        battery.setOrientation(LinearLayout.VERTICAL);
        battery.setPadding(dp(8), 0, 0, 0);

        batteryPercent = text("--", 11, COLOR_TEXT,
                Typeface.create("sans-serif-medium", Typeface.BOLD));
        batteryPercent.setSingleLine(true);
        battery.addView(batteryPercent, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        batteryState = text("", 8, COLOR_MUTED,
                Typeface.create("sans-serif", Typeface.NORMAL));
        batteryState.setSingleLine(true);
        battery.addView(batteryState, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        cluster.addView(battery, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        return cluster;
    }

    private void updateClock(long nowMillis) {
        if (clockTime == null || clockDate == null) {
            return;
        }
        clockTime.setText(DeviceStatusMonitor.formatTime(this, nowMillis));
        clockDate.setText(DeviceStatusMonitor.formatDate(nowMillis));
    }

    private void updateBattery(BatteryReading reading) {
        if (batteryGauge == null || batteryPercent == null || batteryState == null) {
            return;
        }
        batteryGauge.setReading(reading);
        batteryPercent.setText(reading.percentText());
        batteryState.setText(reading.stateText());

        int color;
        switch (reading.severity()) {
            case CRITICAL:
                color = COLOR_ERROR;
                break;
            case LOW:
                color = COLOR_WARNING;
                break;
            default:
                color = COLOR_TEXT;
                break;
        }
        batteryPercent.setTextColor(color);
    }

    private String versionName() {
        if (cachedVersionName == null) {
            String value = FALLBACK_VERSION_NAME;
            try {
                PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
                if (info.versionName != null && !info.versionName.trim().isEmpty()) {
                    value = info.versionName;
                }
            } catch (PackageManager.NameNotFoundException | RuntimeException ignored) {
                // Fall back to the compiled-in name.
            }
            cachedVersionName = value;
        }
        return cachedVersionName;
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
                "EM DESENVOLVIMENTO", 8, 0xFFFFA5A8,
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
                value, 9, COLOR_MUTED,
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

    private String installationDirectoryName(String serial) {
        return "gow_" + serial.toLowerCase(Locale.ROOT)
                .replaceAll("[^a-z0-9]+", "_").replaceAll("^_+|_+$", "");
    }

    private void onPlayPressed() {
        if (installationReadyForBoot) {
            openRuntimeDiagnostics();
            return;
        }

        if (selectedDiscUri == null
                && selectedDiscValid
                && selectedDiscSerial != null
                && selectedDiscBootPath != null
                && !selectedDiscBootPath.isEmpty()) {
            File installation = new File(
                    new File(getFilesDir(), "installations"),
                    installationDirectoryName(selectedDiscSerial));
            if (installation.isDirectory()) {
                showExistingInstallationVerification(installation, selectedDiscBootPath);
                return;
            }
        }

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

        if (!selectedDiscValid || selectedDiscBootPath == null
                || selectedDiscBootPath.isEmpty()) {
            new AlertDialog.Builder(this)
                    .setTitle("Imagem não validada")
                    .setMessage("A imagem precisa passar pela validação ISO9660 e pela leitura de SYSTEM.CNF antes da importação.")
                    .setPositiveButton("Entendi", null)
                    .setNeutralButton("Trocar imagem",
                            (dialog, which) ->
                                    openDocumentLauncher.launch(new String[]{"*/*"}))
                    .show();
            return;
        }

        File installationRoot = new File(getFilesDir(), "installations");
        File existingInstallation = new File(
                installationRoot, installationDirectoryName(selectedDiscSerial));
        if (existingInstallation.exists()) {
            showExistingInstallationVerification(
                    existingInstallation, selectedDiscBootPath);
            return;
        }

        new AlertDialog.Builder(this)
                .setTitle("Preparar instalação do disco")
                .setMessage(
                        "Disco reconhecido: " + selectedDiscSerial + ".\n\n"
                                + "O FORGE vai copiar os arquivos ISO9660 para o armazenamento "
                                + "privado e gerar .disc_layout e .disc_meta, que preservam as "
                                + "leituras do disco por LSN usadas pelo runtime PS2. Esta etapa "
                                + "não inicia o jogo. A instalação só será publicada quando "
                                + "todos os arquivos e metadados forem concluídos.")
                .setPositiveButton("Preparar", (dialog, which) -> startFullDiscInstall())
                .setNegativeButton("Cancelar", null)
                .show();
    }

    private void showExistingInstallationVerification(
            File installation, String bootPath) {
        new AlertDialog.Builder(this)
                .setTitle("Verificar disco instalado")
                .setMessage(
                        "Já existe uma instalação privada para " + selectedDiscSerial
                                + ". O FORGE vai conferir o ELF, os metadados do disco "
                                + "e o serviço CDVD antes de permitir a próxima etapa. "
                                + "Esta verificação não inicia o jogo nem copia a ISO novamente.")
                .setPositiveButton(
                        "Verificar",
                        (dialog, which) ->
                                verifyExistingInstallationAsync(installation, bootPath))
                .setNegativeButton("Cancelar", null)
                .show();
    }

    private void verifyExistingInstallationAsync(
            File finalInstallation, String bootPath) {
        if (installingDisc) return;

        installationReadyForBoot = false;
        installingDisc = true;
        cancelDiscInstallRequested = false;
        showDiscInstallProgressDialog(
                "Verificando instalação existente",
                "Lendo o manifesto de integridade SHA-256...");
        if (installDiscButton != null) {
            installDiscButton.setEnabled(false);
            installDiscButton.setText("VERIFICANDO...");
            installDiscButton.setAlpha(0.6f);
        }
        discStatus.setText("Verificando a integridade dos arquivos instalados...");
        discStatus.setTextColor(COLOR_MUTED);

        ioExecutor.execute(() -> {
            String integrityValidation;
            String runtimePathValidation = "A verificação SHA-256 precisa passar antes de configurar IoPaths.";
            String elfValidation = "A validação de IoPaths precisa passar antes de validar o ELF.";
            String discValidation = "A validação ELF precisa passar antes de abrir o DiscSource.";
            String iopCdvdValidation = "A validação DiscSource precisa passar antes do probe CDVD do IOP.";
            boolean cancelled = false;
            try {
                File cdRoot = new File(finalInstallation, "disc");
                integrityValidation = nativeVerifyDiscIntegrity(
                        cdRoot.getAbsolutePath(), createIntegrityProgressCallback());
                cancelled = integrityValidation != null
                        && integrityValidation.startsWith("CANCELLED:");
                boolean integrityValid = integrityValidation != null
                        && integrityValidation.startsWith("OK:");
                boolean cdRootReady = integrityValid
                        && nativeSetCdRootDirectory(cdRoot.getAbsolutePath());
                if (!integrityValid) {
                    if (cancelled) {
                        runtimePathValidation = "A verificação foi cancelada pelo usuário.";
                    } else {
                        runtimePathValidation = "A integridade SHA-256 precisa passar antes de configurar IoPaths.";
                    }
                } else if (!cdRootReady) {
                    runtimePathValidation = "O provider cdRoot não conseguiu abrir a pasta privada.";
                } else {
                    runtimePathValidation = nativePrepareRuntimeIoPaths(bootPath);
                    if (runtimePathValidation != null && runtimePathValidation.startsWith("OK:")) {
                        elfValidation = nativeValidateCdRootElf(bootPath);
                        if (elfValidation != null && elfValidation.startsWith("OK:")) {
                            discValidation = nativeValidateDiscSource(bootPath);
                            if (discValidation != null && discValidation.startsWith("OK:")) {
                                iopCdvdValidation = nativeValidateIopCdvd(bootPath);
                            }
                        }
                    }
                }
            } catch (RuntimeException exception) {
                integrityValidation = "Falha durante a verificação: " + safeMessage(exception);
            }

            final String finalIntegrity = integrityValidation;
            final String finalRuntimePath = runtimePathValidation;
            final String finalElf = elfValidation;
            final String finalDisc = discValidation;
            final String finalCdvd = iopCdvdValidation;
            final boolean finalCancelled = cancelled
                    || (cancelDiscInstallRequested
                        && finalIntegrity != null
                        && finalIntegrity.startsWith("CANCELLED:"));
            final boolean installationValid =
                    finalIntegrity != null && finalIntegrity.startsWith("OK:")
                    && finalRuntimePath != null && finalRuntimePath.startsWith("OK:")
                    && finalElf != null && finalElf.startsWith("OK:")
                    && finalDisc != null && finalDisc.startsWith("OK:")
                    && finalCdvd != null && finalCdvd.startsWith("OK:");
            runOnUiThread(() -> finishExistingInstallationValidation(
                    installationValid, finalCancelled, finalIntegrity,
                    finalRuntimePath, finalElf, finalDisc, finalCdvd));
        });
    }

    private InstallProgressCallback createIntegrityProgressCallback() {
        final int[] lastPercent = {-1};
        return (totalBytes, verifiedBytes, completedFiles, totalFiles) -> {
            if (cancelDiscInstallRequested) return false;
            int percent = totalBytes <= 0L ? 0
                    : (int) Math.min(100L, (verifiedBytes * 100L) / totalBytes);
            if (percent != lastPercent[0]) {
                lastPercent[0] = percent;
                final String progressText = totalFiles <= 0
                        ? "Verificando SHA-256: " + percent + "%"
                        : percent + "% · " + completedFiles + "/" + totalFiles + " arquivos verificados";
                runOnUiThread(() -> {
                    if (!isFinishing() && !isDestroyed()
                            && discInstallProgressBar != null) {
                        discStatus.setText("Verificando integridade: " + progressText);
                        discInstallProgressBar.setProgress(percent);
                        if (discInstallProgressText != null) {
                            discInstallProgressText.setText(progressText);
                        }
                    }
                });
            }
            return !cancelDiscInstallRequested;
        };
    }

    private void finishExistingInstallationValidation(
            boolean installationValid, boolean cancelled,
            String integrityValidation, String runtimePathValidation,
            String elfValidation, String discValidation, String iopCdvdValidation) {
        if (isFinishing() || isDestroyed()) return;
        if (discInstallDialog != null) {
            discInstallDialog.dismiss();
            discInstallDialog = null;
        }
        discInstallProgressBar = null;
        discInstallProgressText = null;
        installingDisc = false;
        installationReadyForBoot = installationValid && !cancelled;
        if (installDiscButton != null) {
            installDiscButton.setEnabled(true);
            installDiscButton.setAlpha(1f);
            installDiscButton.setText(installationReadyForBoot ? "JOGAR" : "VERIFICAR DISCO");
            installDiscButton.setContentDescription(
                    installationReadyForBoot
                            ? "Tentar o primeiro boot nativo de God of War"
                            : "Verificar a instalação privada existente de God of War");
        }

        if (cancelled) {
            discStatus.setText("Verificação de integridade cancelada; nenhum arquivo foi alterado.");
            new AlertDialog.Builder(this)
                    .setTitle("Verificação cancelada")
                    .setMessage("A leitura SHA-256 foi interrompida. A instalação não foi aprovada nem modificada. Você pode verificar novamente.")
                    .setPositiveButton("Continuar", null)
                    .show();
            return;
        }

        String detail = installationValid
                ? "Integridade: " + integrityValidation.substring(4).trim()
                        + ". IoPaths do runtime: " + runtimePathValidation.substring(4).trim()
                        + ". Executável: " + elfValidation.substring(4).trim()
                        + ". Fonte CD/DVD: " + discValidation.substring(4).trim()
                        + ". CDVD/IOP: " + iopCdvdValidation.substring(4).trim() + "."
                : "Integridade SHA-256: " + integrityValidation
                        + "\nIoPaths: " + runtimePathValidation
                        + "\nELF: " + elfValidation
                        + "\nDiscSource: " + discValidation
                        + "\nCDVD/IOP: " + iopCdvdValidation;
        String runtimeStatus = installationValid
                ? "DiscSource e o dispatcher CDVD do IOP passaram na verificação privada. Toque em JOGAR para abrir a tentativa de boot nativo. A execução depende de este APK conter a tabela AOT privada gerada a partir da ISO; sem ela, o painel exibirá o diagnóstico. A primeira tentativa é silenciosa, pois o áudio Android ainda não está integrado."
                : "A instalação não será utilizada enquanto a validação falhar. Você pode solicitar uma reinstalação dos arquivos desta revisão.";
        AlertDialog.Builder resultDialog = new AlertDialog.Builder(this)
                .setTitle(installationValid
                        ? "Instalação do disco verificada"
                        : "Instalação existente incompleta")
                .setMessage("A pasta de instalação já existe no armazenamento privado. "
                        + detail + "\n\n" + runtimeStatus);
        if (installationValid) {
            discStatus.setText("Instalação verificada. Toque em JOGAR para tentar o primeiro boot nativo.");
            resultDialog.setPositiveButton(
                    "JOGAR",
                    (dialog, which) -> openRuntimeDiagnostics());
        } else {
            discStatus.setText("A instalação precisa ser reparada antes de qualquer execução.");
            if (selectedDiscUri != null) {
                resultDialog.setPositiveButton("Reinstalar", (dialog, which) ->
                        confirmInstallationRepair());
            } else {
                resultDialog.setPositiveButton(
                        "Selecionar ISO",
                        (dialog, which) ->
                                openDocumentLauncher.launch(new String[]{"*/*"}));
            }
            resultDialog.setNegativeButton("Cancelar", null);
        }
        resultDialog.show();
    }

    private void startFullDiscInstall() {
        startFullDiscInstall(false);
    }

    private void startFullDiscInstall(boolean repairExisting) {
        if (installingDisc || selectedDiscUri == null
                || selectedDiscBootPath == null || !selectedDiscValid) {
            return;
        }

        installationReadyForBoot = false;
        final Uri sourceUri = selectedDiscUri;
        final String expectedSerial = selectedDiscSerial;
        final String bootPath = selectedDiscBootPath;
        final String installationName = installationDirectoryName(expectedSerial);
        final File installationRoot = new File(getFilesDir(), "installations");
        final File finalInstallation = new File(installationRoot, installationName);

        if (finalInstallation.exists() && !repairExisting) {
            verifyExistingInstallationAsync(finalInstallation, bootPath);
            return;
        }

        if (!installationRoot.exists() && !installationRoot.mkdirs()) {
            showDiscInstallResult(false,
                    "Não foi possível criar a pasta privada de instalação.");
            return;
        }

        installingDisc = true;
        cancelDiscInstallRequested = false;
        showDiscInstallProgressDialog();
        if (installDiscButton != null) {
            installDiscButton.setEnabled(false);
            installDiscButton.setText("INSTALANDO...");
            installDiscButton.setAlpha(0.6f);
        }
        discStatus.setText("Analisando e preparando os dados do disco...");
        discStatus.setTextColor(COLOR_MUTED);

        ioExecutor.execute(() -> {
            ParcelFileDescriptor candidate = null;
            String failure = null;
            try {
                candidate = getContentResolver().openFileDescriptor(sourceUri, "r");
                if (candidate == null) {
                    throw new IOException("O provedor não conseguiu reabrir a imagem selecionada.");
                }

                String[] detectedBootPath = new String[1];
                String detectedSerial = identifyGodOfWarDisc(candidate, detectedBootPath);
                if (!expectedSerial.equals(detectedSerial)
                        || !bootPath.equals(detectedBootPath[0])) {
                    throw new IOException(
                            "A identidade da imagem mudou desde a validação inicial. "
                                    + "Selecione e valide novamente o disco.");
                }

                if (repairExisting) {
                    String removalError = nativeRemoveInstallationDirectory(
                            installationRoot.getAbsolutePath(), installationName);
                    if (removalError != null && !removalError.isEmpty()) {
                        throw new IOException(
                                "A instalação anterior não foi removida: " + removalError);
                    }
                }

                final int[] lastPercent = {-1};
                final long installStartedAt = SystemClock.elapsedRealtime();
                final long[] lastProgressSampleAt = {installStartedAt};
                final long[] lastProgressSampleBytes = {0L};
                final long[] lastProgressUiAt = {0L};
                final double[] smoothedBytesPerSecond = {0.0};
                InstallProgressCallback callback =
                        (totalBytes, copiedBytes, completedFiles, totalFiles) -> {
                            if (cancelDiscInstallRequested) {
                                return false;
                            }
                            int percent = totalBytes <= 0L ? 0
                                    : (int) Math.min(100L,
                                            (copiedBytes * 100L) / totalBytes);
                            long now = SystemClock.elapsedRealtime();
                            long sampleElapsed = now - lastProgressSampleAt[0];
                            if (sampleElapsed >= 500L) {
                                long byteDelta = Math.max(
                                        0L, copiedBytes - lastProgressSampleBytes[0]);
                                if (byteDelta > 0L) {
                                    double sampleRate = byteDelta * 1000.0 / sampleElapsed;
                                    smoothedBytesPerSecond[0] =
                                            smoothedBytesPerSecond[0] <= 0.0
                                                    ? sampleRate
                                                    : smoothedBytesPerSecond[0] * 0.70
                                                            + sampleRate * 0.30;
                                }
                                lastProgressSampleAt[0] = now;
                                lastProgressSampleBytes[0] = copiedBytes;
                            }
                            if (percent != lastPercent[0]
                                    || now - lastProgressUiAt[0] >= 1000L
                                    || (totalFiles > 0 && completedFiles >= totalFiles)) {
                                lastPercent[0] = percent;
                                lastProgressUiAt[0] = now;
                                final String progressText;
                                if (totalFiles <= 0) {
                                    progressText = "Analisando setores do disco: " + percent + "%";
                                } else {
                                    double mibPerSecond =
                                            smoothedBytesPerSecond[0] / (1024.0 * 1024.0);
                                    long remainingBytes = Math.max(0L, totalBytes - copiedBytes);
                                    long etaSeconds = smoothedBytesPerSecond[0] > 0.0
                                            ? (long) Math.ceil(
                                                    remainingBytes / smoothedBytesPerSecond[0])
                                            : -1L;
                                    String rateText = mibPerSecond > 0.0
                                            ? String.format(
                                                    Locale.getDefault(), "%.1f MB/s", mibPerSecond)
                                            : "calculando velocidade...";
                                    String etaText = etaSeconds >= 0L
                                            ? formatRemainingTime(etaSeconds)
                                            : "calculando tempo...";
                                    progressText = percent + "% · " + completedFiles + "/"
                                            + totalFiles + " arquivos\n"
                                            + String.format(
                                                    Locale.getDefault(),
                                                    "%.2f/%.2f GB",
                                                    copiedBytes / (1024.0 * 1024.0 * 1024.0),
                                                    totalBytes / (1024.0 * 1024.0 * 1024.0))
                                            + " · " + rateText + " · restante " + etaText;
                                }
                                runOnUiThread(() -> {
                                    if (!isFinishing() && !isDestroyed()) {
                                        discStatus.setText("Preparando instalação: " + progressText);
                                        if (discInstallProgressBar != null) {
                                            discInstallProgressBar.setProgress(percent);
                                        }
                                        if (discInstallProgressText != null) {
                                            discInstallProgressText.setText(progressText);
                                        }
                                    }
                                });
                            }
                            return !cancelDiscInstallRequested;
                        };

                String installSummary = nativeInstallIsoDisc(
                        candidate.getFd(), installationRoot.getAbsolutePath(),
                        installationName, callback);
                if (installSummary == null || !installSummary.startsWith("OK:")) {
                    failure = installSummary == null || installSummary.isEmpty()
                            ? "A instalação do disco falhou sem fornecer um diagnóstico."
                            : installSummary;
                } else if (!nativeSetCdRootDirectory(
                        new File(finalInstallation, "disc").getAbsolutePath())) {
                    failure = "Os arquivos foram publicados, mas o provider cdRoot não conseguiu abrir a pasta privada.";
                } else {
                    // Every extracted byte was SHA-256 hashed in the same buffer
                    // that was written, and every output file was fsync'd before
                    // the staging directory was published. Avoid rereading the
                    // full multi-gigabyte installation immediately after copy.
                    // Existing installations still receive a full readback hash
                    // verification in verifyExistingInstallationAsync().
                    String runtimePathValidation = nativePrepareRuntimeIoPaths(bootPath);
                    if (runtimePathValidation == null || !runtimePathValidation.startsWith("OK:")) {
                        failure = "A instalação foi publicada, mas PS2Runtime não aceitou o caminho físico "
                                + "do cdRoot: " + (runtimePathValidation == null
                                    ? "resposta nativa vazia." : runtimePathValidation);
                    } else {
                        String elfValidation = nativeValidateCdRootElf(bootPath);
                        if (elfValidation == null || !elfValidation.startsWith("OK:")) {
                            failure = "O arquivo de BOOT2 foi copiado, mas falhou na validação ELF local: "
                                    + (elfValidation == null ? "resposta nativa vazia." : elfValidation);
                        } else {
                            String discValidation = nativeValidateDiscSource(bootPath);
                            if (discValidation == null || !discValidation.startsWith("OK:")) {
                                failure = "Os arquivos foram extraídos e o ELF é válido, mas a fonte CD/DVD "
                                        + "do runtime não conseguiu reconstruir o disco: "
                                        + (discValidation == null ? "resposta nativa vazia." : discValidation);
                            } else {
                                String iopCdvdValidation = nativeValidateIopCdvd(bootPath);
                                if (iopCdvdValidation == null || !iopCdvdValidation.startsWith("OK:")) {
                                    failure = "O DiscSource foi aberto, mas o dispatcher CDVD do IOP não "
                                            + "conseguiu ler o BOOT2: "
                                            + (iopCdvdValidation == null
                                                ? "resposta nativa vazia." : iopCdvdValidation);
                                } else {
                                    preparedDiscSummary = installSummary.substring(3).trim()
                                            + " · " + runtimePathValidation.substring(4).trim()
                                            + " · " + elfValidation.substring(4).trim()
                                            + " · " + discValidation.substring(4).trim()
                                            + " · " + iopCdvdValidation.substring(4).trim();
                                }
                            }
                        }
                    }
                }
            } catch (IOException | SecurityException | ErrnoException exception) {
                failure = "Falha na importação: " + safeMessage(exception);
            } catch (RuntimeException exception) {
                failure = "Falha na importação: " + safeMessage(exception);
            } finally {
                closeQuietly(candidate);
            }

            final String finalFailure = failure;
            runOnUiThread(() -> showDiscInstallResult(
                    finalFailure == null, finalFailure));
        });
    }

    private void confirmInstallationRepair() {
        new AlertDialog.Builder(this)
                .setTitle("Confirmar reinstalação")
                .setMessage(
                        "A pasta privada da instalação atual será removida e recriada "
                                + "a partir da imagem selecionada. Outros arquivos e instalações "
                                + "não serão afetados. A reinstalação só começa depois de a imagem "
                                + "ser reaberta e sua identidade ser confirmada.")
                .setPositiveButton("Apagar e reinstalar", (dialog, which) ->
                        startFullDiscInstall(true))
                .setNegativeButton("Cancelar", null)
                .show();
    }

    private static String formatRemainingTime(long seconds) {
        long safeSeconds = Math.max(0L, seconds);
        if (safeSeconds < 60L) {
            return safeSeconds + " s";
        }
        long minutes = safeSeconds / 60L;
        long remainingSeconds = safeSeconds % 60L;
        if (minutes < 60L) {
            return String.format(
                    Locale.getDefault(), "%d min %02d s", minutes, remainingSeconds);
        }
        long hours = minutes / 60L;
        long remainingMinutes = minutes % 60L;
        return String.format(
                Locale.getDefault(), "%d h %02d min", hours, remainingMinutes);
    }

    private void showDiscInstallProgressDialog() {
        showDiscInstallProgressDialog(
                "Preparando instalação do disco", "Analisando a estrutura do disco...");
    }

    private void showDiscInstallProgressDialog(String title, String initialMessage) {
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(20), dp(14), dp(20), dp(8));

        discInstallProgressText = text(
                initialMessage, 10, COLOR_MUTED,
                Typeface.create("sans-serif", Typeface.NORMAL));
        content.addView(discInstallProgressText, matchWrap());

        discInstallProgressBar = new ProgressBar(
                this, null, android.R.attr.progressBarStyleHorizontal);
        discInstallProgressBar.setIndeterminate(false);
        discInstallProgressBar.setMax(100);
        discInstallProgressBar.setProgress(0);
        LinearLayout.LayoutParams progressParams = matchWrap();
        progressParams.topMargin = dp(13);
        content.addView(discInstallProgressBar, progressParams);

        discInstallDialog = new AlertDialog.Builder(this)
                .setTitle(title)
                .setView(content)
                .setNegativeButton("Cancelar", null)
                .create();
        discInstallDialog.setCancelable(false);
        discInstallDialog.setCanceledOnTouchOutside(false);
        discInstallDialog.setOnShowListener(dialog -> {
            Button cancelButton = discInstallDialog.getButton(AlertDialog.BUTTON_NEGATIVE);
            cancelButton.setOnClickListener(view -> {
                cancelDiscInstallRequested = true;
                cancelButton.setEnabled(false);
                if (discInstallProgressText != null) {
                    discInstallProgressText.setText("Cancelando a operação com segurança...");
                }
            });
        });
        discInstallDialog.show();
    }

    private void showDiscInstallResult(boolean success, String error) {
        if (isFinishing() || isDestroyed()) {
            return;
        }
        if (discInstallDialog != null) {
            discInstallDialog.dismiss();
            discInstallDialog = null;
        }
        discInstallProgressBar = null;
        discInstallProgressText = null;
        installingDisc = false;
        installationReadyForBoot = success;
        if (installDiscButton != null) {
            installDiscButton.setEnabled(true);
            installDiscButton.setAlpha(1f);
            installDiscButton.setText(success ? "JOGAR" : "TENTAR NOVAMENTE");
            installDiscButton.setContentDescription(
                    success
                            ? "Tentar o primeiro boot nativo de God of War"
                            : "Tentar a instalação novamente");
        }

        if (success) {
            discStatus.setText(
                    "Instalação completa do disco preparada. "
                            + preparedDiscSummary + ". O boot do jogo ainda não foi iniciado.");
            discStatus.setTextColor(0xFF8BC28F);
            new AlertDialog.Builder(this)
                    .setTitle("Instalação do disco concluída")
                    .setMessage(
                            "Os arquivos ISO9660 foram extraídos para o armazenamento "
                                    + "privado do aplicativo. Os metadados .disc_layout e "
                                    + ".disc_meta foram gerados e publicados junto com os dados. "
                                    + preparedDiscSummary + ". A instalação foi preparada e a "
                                    + "fonte de disco foi validada. Toque em JOGAR para abrir a "
                                    + "tentativa de boot nativo. Ela só poderá executar se este "
                                    + "APK incluir a tabela AOT privada gerada a partir da ISO; "
                                    + "caso contrário, o painel mostrará o diagnóstico. A primeira "
                                    + "tentativa é silenciosa, pois o áudio Android ainda não está integrado.")
                    .setPositiveButton(
                            "JOGAR",
                            (dialog, which) -> openRuntimeDiagnostics())
                    .show();
        } else {
            String message = error == null || error.isEmpty()
                    ? "Não foi possível preparar a instalação do disco."
                    : error;
            boolean cancelled = message.startsWith("Instalação cancelada");
            discStatus.setText(cancelled
                    ? "Instalação cancelada; os arquivos temporários foram removidos."
                    : "Falha na instalação: " + message);
            discStatus.setTextColor(cancelled ? COLOR_MUTED : 0xFFE27676);
            new AlertDialog.Builder(this)
                    .setTitle(cancelled ? "Instalação cancelada" : "Falha na instalação")
                    .setMessage(message)
                    .setPositiveButton("Fechar", null)
                    .show();
        }
    }

    private void openRuntimeDiagnostics() {
        if (selectedDiscSerial == null || selectedDiscSerial.isEmpty()
                || selectedDiscBootPath == null || selectedDiscBootPath.isEmpty()) {
            new AlertDialog.Builder(this)
                    .setTitle("BOOT2 indisponível")
                    .setMessage("Valide novamente o disco para recuperar o caminho BOOT2 antes de abrir o diagnóstico.")
                    .setPositiveButton("Fechar", null)
                    .show();
            return;
        }

        File cdRoot = new File(
                new File(
                        new File(getFilesDir(), "installations"),
                        installationDirectoryName(selectedDiscSerial)),
                "disc");
        if (!cdRoot.isDirectory()) {
            new AlertDialog.Builder(this)
                    .setTitle("Instalação privada não encontrada")
                    .setMessage("A pasta do disco não existe. Reinstale e valide a imagem antes de continuar.")
                    .setPositiveButton("Fechar", null)
                    .show();
            return;
        }

        Intent intent = new Intent(this, GowGameActivity.class);
        intent.putExtra(
                GowGameActivity.EXTRA_PRIVATE_CD_ROOT,
                cdRoot.getAbsolutePath());
        intent.putExtra(
                GowGameActivity.EXTRA_BOOT_RELATIVE_PATH,
                selectedDiscBootPath);
        startActivity(intent);
    }

    private void showSettingsDialog() {
        new AlertDialog.Builder(this)
                .setTitle("Configurações")
                .setMessage(
                        "PLATAFORMA\nAndroid / ARM64\n\n"
                                + "GRÁFICOS\nVulkan (renderizador do jogo em desenvolvimento)\n\n"
                                + "INTERFACE\nFORGE\n\n"
                                + "VERSÃO\n" + versionName() + "\n\n"
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
                                + "A validação gráfica será feita durante a execução do jogo.\n\n"
                                + "Referências de engenharia: Black para Android e Dragon Rage / "
                                + "Budokai Tenkaichi 3. Os códigos desses projetos não foram incorporados. "
                                + "Consulte THIRD_PARTY_NOTICES.md no repositório.")
                .setPositiveButton("Fechar", null)
                .show();
    }

    private void onDiscSelected(Uri uri) {
        if (uri == null) {
            return;
        }

        selectedDiscUri = uri;
        selectedDiscValid = false;
        installationReadyForBoot = false;
        selectedDiscBootPath = null;
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

    private boolean restoreSelectedDisc() {
        String savedUri = getPreferences(MODE_PRIVATE)
                .getString(PREF_DISC_URI, null);
        if (savedUri == null || savedUri.isEmpty()) {
            return false;
        }
        selectedDiscUri = Uri.parse(savedUri);
        validateAndDisplay(selectedDiscUri);
        return true;
    }

    /**
     * Recovers the installed NTSC-U game without requiring the original SAF URI.
     * This is used after a safe Debug-signing migration or when the user no
     * longer has access to the document provider that supplied the ISO.
     */
    private boolean restorePrivateInstallationIfAvailable() {
        File installation = new File(
                new File(getFilesDir(), "installations"),
                "gow_scus_973_99_ntsc_u");
        File cdRoot = new File(installation, "disc");
        File systemCnf = new File(cdRoot, "SYSTEM.CNF");
        if (!systemCnf.isFile() || systemCnf.length() <= 0L
                || systemCnf.length() > 64L * 1024L) {
            return false;
        }

        try (InputStream input = new FileInputStream(systemCnf)) {
            byte[] bytes = new byte[(int) systemCnf.length()];
            int total = 0;
            while (total < bytes.length) {
                int count = input.read(bytes, total, bytes.length - total);
                if (count <= 0) {
                    throw new IOException("SYSTEM.CNF privado foi truncado.");
                }
                total += count;
            }
            String bootPath = parseSystemCnfBootPath(
                    new String(bytes, StandardCharsets.US_ASCII));
            if (!"SCUS_973.99".equals(new File(bootPath).getName())) {
                throw new IOException(
                        "A instalação privada não pertence ao perfil NTSC-U suportado.");
            }
            selectedDiscUri = null;
            selectedDiscValid = true;
            installationReadyForBoot = false;
            selectedDiscSerial = "SCUS_973.99 (NTSC-U)";
            selectedDiscBootPath = bootPath;
            discName.setText("God of War (instalação privada)");
            discStatus.setText(
                    "Instalação privada NTSC-U encontrada. A integridade SHA-256 "
                            + "ainda precisa ser verificada antes de abrir o runtime.");
            discStatus.setTextColor(COLOR_WARNING);
            if (installDiscButton != null) {
                installDiscButton.setText("VERIFICAR DISCO");
                installDiscButton.setContentDescription(
                        "Verificar a instalação privada existente de God of War");
            }
            return true;
        } catch (IOException | SecurityException exception) {
            discStatus.setText(
                    "Foi encontrada uma instalação privada, mas SYSTEM.CNF não pôde "
                            + "ser validado: " + safeMessage(exception));
            discStatus.setTextColor(COLOR_ERROR);
            return false;
        }
    }

    private void validateAndDisplay(Uri uri) {
        if (selectDiscButton != null) {
            selectDiscButton.setEnabled(false);
            selectDiscButton.setAlpha(0.55f);
        }
        discName.setText("Verificando imagem...");
        discStatus.setText("Lendo ISO9660 e identificando SYSTEM.CNF.");
        discStatus.setTextColor(COLOR_MUTED);
        installationReadyForBoot = false;
        selectedDiscValid = false;
        selectedDiscSerial = null;
        selectedDiscBootPath = null;
        ioExecutor.execute(() -> {
            String name = queryDisplayName(uri);
            String result;
            boolean valid = false;
            ParcelFileDescriptor candidate = null;
            String serial = null;
            String[] bootPath = new String[1];

            try {
                candidate = getContentResolver().openFileDescriptor(uri, "r");
                if (candidate == null) {
                    throw new IOException("O provedor não conseguiu abrir o arquivo.");
                }

                serial = identifyGodOfWarDisc(candidate, bootPath);
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
            final String finalBootPath = bootPath[0];
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) {
                    return;
                }
                selectedDiscValid = isValid;
                selectedDiscSerial = finalSerial;
                selectedDiscBootPath = isValid ? finalBootPath : null;
                discName.setText(finalName);
                discStatus.setText(finalResult);
                discStatus.setTextColor(
                        isValid ? 0xFF8BC28F : 0xFFE27676);
                if (installDiscButton != null) {
                    File privateInstallation = isValid
                            ? new File(
                                    new File(getFilesDir(), "installations"),
                                    installationDirectoryName(finalSerial))
                            : null;
                    boolean alreadyInstalled = privateInstallation != null
                            && privateInstallation.isDirectory();
                    installationReadyForBoot = false;
                    installDiscButton.setText(
                            alreadyInstalled ? "VERIFICAR DISCO" : "INSTALAR JOGO");
                    installDiscButton.setContentDescription(
                            alreadyInstalled
                                    ? "Verificar a instalação privada existente de God of War"
                                    : "Instalar os dados do disco selecionado");
                }
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
    private String identifyGodOfWarDisc(
            ParcelFileDescriptor descriptor, String[] executablePathOut)
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
        String normalizedBootPath = parseSystemCnfBootPath(text);
        int slash = normalizedBootPath.lastIndexOf('/');
        String executable = slash >= 0
                ? normalizedBootPath.substring(slash + 1)
                : normalizedBootPath;
        String serial = executable.toUpperCase(Locale.ROOT);
        if (executablePathOut == null || executablePathOut.length == 0) {
            throw new IOException("Não foi possível armazenar o caminho BOOT2.");
        }
        executablePathOut[0] = normalizedBootPath;

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

    private static String parseSystemCnfBootPath(String systemCnfText)
            throws IOException {
        String bootTarget = null;
        for (String line : systemCnfText.split("\\r?\\n")) {
            int equals = line.indexOf('=');
            if (equals < 0) continue;
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
            String device = executablePath.substring(0, colon).trim();
            if (!"cdrom0".equalsIgnoreCase(device)) {
                throw new IOException("SYSTEM.CNF aponta para um dispositivo BOOT2 não suportado.");
            }
            executablePath = executablePath.substring(colon + 1);
        }
        return normalizeIsoPathForInstall(executablePath);
    }

    private static String normalizeIsoPathForInstall(String path) throws IOException {
        String normalized = path.replace('\\', '/').trim();
        while (normalized.startsWith("/")) {
            normalized = normalized.substring(1);
        }
        if (normalized.isEmpty() || normalized.length() > 1024) {
            throw new IOException("O caminho BOOT2 está vazio ou excede o limite suportado.");
        }

        String[] components = normalized.split("/", -1);
        if (components.length > 32) {
            throw new IOException("O caminho BOOT2 excede o limite de profundidade.");
        }
        StringBuilder result = new StringBuilder();
        for (String component : components) {
            if (component.isEmpty() || ".".equals(component) || "..".equals(component)
                    || component.length() > 255 || component.indexOf(':') >= 0) {
                throw new IOException("O caminho BOOT2 contém um componente inválido.");
            }
            int version = component.indexOf(';');
            String name = version >= 0 ? component.substring(0, version) : component;
            if (name.isEmpty()) {
                throw new IOException("O caminho BOOT2 contém um nome vazio.");
            }
            if (result.length() > 0) result.append('/');
            result.append(name.toUpperCase(Locale.ROOT));
        }
        return result.toString();
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
        if (deviceStatusMonitor != null) {
            deviceStatusMonitor.stop();
        }
        cancelDiscInstallRequested = true;
        ioExecutor.shutdownNow();
        if (discInstallDialog != null) {
            discInstallDialog.dismiss();
            discInstallDialog = null;
        }
        super.onDestroy();
    }
}

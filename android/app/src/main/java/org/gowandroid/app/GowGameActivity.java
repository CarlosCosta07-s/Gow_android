package org.gowandroid.app;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.content.pm.ApplicationInfo;
import android.os.Bundle;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import com.google.androidgamesdk.GameActivity;


/**
 * Native entry point reserved for the God of War runtime.
 *
 * The Activity displays native startup diagnostics so an unavailable runtime
 * is not presented as an unexplained blank screen. It is not a playable game
 * runtime until private AOT code, GS/Vulkan presentation and Android audio are
 * integrated.
 */
public final class GowGameActivity extends GameActivity {
    public static final String EXTRA_DISC_FD =
            "org.gowandroid.app.extra.DISC_FD";
    public static final String EXTRA_PRIVATE_CD_ROOT =
            "org.gowandroid.app.extra.PRIVATE_CD_ROOT";
    public static final String EXTRA_BOOT_RELATIVE_PATH =
            "org.gowandroid.app.extra.BOOT_RELATIVE_PATH";

    private static native boolean nativeSetDiscFileDescriptor(int descriptor);
    private static native boolean nativeConfigurePrivateBoot(
            String privateCdRoot, String relativePath);
    private static native void nativeSetActivityReference(GowGameActivity activity);
    private static native void nativeClearActivityReference(GowGameActivity activity);
    private static native boolean nativeRequestGsSceneCapture();
    private static native void nativeSetTouchButtons(int pressedMask);

    private TextView nativeStatusView;
    private String pendingNativeStatus;
    private boolean pendingNativeStatusError;
    private int touchButtonsMask;

    private static final int PAD_LEFT = 0x0080;
    private static final int PAD_DOWN = 0x0040;
    private static final int PAD_RIGHT = 0x0020;
    private static final int PAD_UP = 0x0010;
    private static final int PAD_START = 0x0008;
    private static final int PAD_SELECT = 0x0001;
    private static final int PAD_SQUARE = 0x8000;
    private static final int PAD_CROSS = 0x4000;
    private static final int PAD_CIRCLE = 0x2000;
    private static final int PAD_TRIANGLE = 0x1000;

    static {
        System.loadLibrary("android-game");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Register before GameActivity starts android_main so the native thread
        // cannot observe a stale BOOT2 configuration from an earlier Activity.
        nativeSetActivityReference(this);
        super.onCreate(savedInstanceState);
        createNativeStatusOverlay();

        int discDescriptor = getIntent().getIntExtra(EXTRA_DISC_FD, -1);
        if (discDescriptor >= 0 && !nativeSetDiscFileDescriptor(discDescriptor)) {
            android.util.Log.e(
                    "GowGameActivity",
                    "Could not initialize the native ISO reader from the supplied descriptor.");
        }

        String privateCdRoot = getIntent().getStringExtra(EXTRA_PRIVATE_CD_ROOT);
        String bootRelativePath = getIntent().getStringExtra(EXTRA_BOOT_RELATIVE_PATH);
        if (privateCdRoot != null && !privateCdRoot.isEmpty()
                && bootRelativePath != null && !bootRelativePath.isEmpty()) {
            if (nativeConfigurePrivateBoot(privateCdRoot, bootRelativePath)) {
                onNativeRuntimeStatus(
                        "Disco privado e BOOT2 encaminhados. Preparando o núcleo e verificando a cobertura AOT...",
                        false);
            } else {
                onNativeRuntimeStatus(
                        "Não foi possível montar a instalação privada ou registrar o BOOT2. O jogo não foi carregado.",
                        true);
            }
        } else {
            // Always release the native startup handshake, including diagnostic-only launches.
            nativeConfigurePrivateBoot("", "");
            onNativeRuntimeStatus(
                    "Nenhuma instalação/BOOT2 foi encaminhada pelo FORGE. Esta tela é somente diagnóstico.",
                    false);
        }

        getWindow().addFlags(
                android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    @Override
    protected void onPause() {
        clearVirtualPad();
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!hasFocus) {
            clearVirtualPad();
        }
    }

    @Override
    protected void onDestroy() {
        clearVirtualPad();
        nativeClearActivityReference(this);
        super.onDestroy();
    }

    private void createNativeStatusOverlay() {
        ViewGroup content = findViewById(android.R.id.content);
        if (content == null) {
            android.util.Log.e(
                    "GowGameActivity",
                    "Could not create native runtime status overlay: content view is missing.");
            return;
        }

        TextView status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(14.0f);
        status.setGravity(Gravity.CENTER_VERTICAL);
        status.setPadding(dp(16), dp(12), dp(16), dp(12));
        status.setMaxLines(5);
        status.setVisibility(View.GONE);
        status.setElevation(dp(16));
        status.setBackground(createStatusBackground(false));
        status.setContentDescription("Status de inicialização do runtime God of War");

        FrameLayout.LayoutParams layout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        layout.setMargins(dp(16), dp(20), dp(16), 0);
        content.addView(status, layout);
        nativeStatusView = status;

        if (pendingNativeStatus != null) {
            nativeStatusView.setText(pendingNativeStatus);
            nativeStatusView.setBackground(
                    createStatusBackground(pendingNativeStatusError));
            nativeStatusView.setVisibility(View.VISIBLE);
            pendingNativeStatus = null;
        }

        boolean isDebuggable = (getApplicationInfo().flags
                & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        if (isDebuggable) {
            Button captureButton = new Button(this);
            captureButton.setText("Capturar cena");
            captureButton.setAllCaps(false);
            captureButton.setContentDescription(
                    "Salvar o frame GS atual para comparação visual");
            captureButton.setOnClickListener(view -> {
                if (nativeRequestGsSceneCapture()) {
                    Toast.makeText(
                            this,
                            "Captura solicitada; será salva no próximo frame GS.",
                            Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(
                            this,
                            "Limite de capturas de depuração atingido.",
                            Toast.LENGTH_SHORT).show();
                }
            });
            FrameLayout.LayoutParams captureLayout = new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    Gravity.TOP | Gravity.END);
            captureLayout.setMargins(0, dp(88), dp(16), 0);
            content.addView(captureButton, captureLayout);
        }

        createTouchControls(content);
    }

    private void createTouchControls(ViewGroup content) {
        LinearLayout dpad = new LinearLayout(this);
        dpad.setOrientation(LinearLayout.VERTICAL);
        dpad.setGravity(Gravity.CENTER);
        dpad.setAlpha(0.78f);
        dpad.addView(createTouchRow(
                createTouchSpacer(), createTouchButton("↑", PAD_UP), createTouchSpacer()));
        dpad.addView(createTouchRow(
                createTouchButton("←", PAD_LEFT), createTouchSpacer(),
                createTouchButton("→", PAD_RIGHT)));
        dpad.addView(createTouchRow(
                createTouchSpacer(), createTouchButton("↓", PAD_DOWN), createTouchSpacer()));
        FrameLayout.LayoutParams dpadLayout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM | Gravity.START);
        dpadLayout.setMargins(dp(10), 0, 0, dp(18));
        content.addView(dpad, dpadLayout);

        LinearLayout face = new LinearLayout(this);
        face.setOrientation(LinearLayout.VERTICAL);
        face.setGravity(Gravity.CENTER);
        face.setAlpha(0.78f);
        face.addView(createTouchRow(
                createTouchButton("△", PAD_TRIANGLE),
                createTouchButton("○", PAD_CIRCLE)));
        face.addView(createTouchRow(
                createTouchButton("□", PAD_SQUARE),
                createTouchButton("×", PAD_CROSS)));
        FrameLayout.LayoutParams faceLayout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM | Gravity.END);
        faceLayout.setMargins(0, 0, dp(10), dp(18));
        content.addView(face, faceLayout);

        LinearLayout menu = new LinearLayout(this);
        menu.setOrientation(LinearLayout.HORIZONTAL);
        menu.setGravity(Gravity.CENTER);
        menu.setAlpha(0.72f);
        menu.addView(createMenuTouchButton("SELECT", PAD_SELECT));
        menu.addView(createMenuTouchButton("START", PAD_START));
        FrameLayout.LayoutParams menuLayout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        menuLayout.setMargins(0, 0, 0, dp(18));
        content.addView(menu, menuLayout);
    }

    private LinearLayout createTouchRow(View first, View second, View third) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        row.addView(first, touchCellLayout());
        row.addView(second, touchCellLayout());
        row.addView(third, touchCellLayout());
        return row;
    }

    private LinearLayout createTouchRow(View first, View second) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        row.addView(first, touchCellLayout());
        row.addView(second, touchCellLayout());
        return row;
    }

    private LinearLayout.LayoutParams touchCellLayout() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(52), dp(52));
        params.setMargins(dp(3), dp(3), dp(3), dp(3));
        return params;
    }

    private View createTouchSpacer() {
        return new View(this);
    }

    private TextView createTouchButton(String label, int mask) {
        TextView button = new TextView(this);
        button.setText(label);
        button.setTextColor(Color.WHITE);
        button.setTextSize(21.0f);
        button.setGravity(Gravity.CENTER);
        button.setBackground(createTouchButtonBackground());
        button.setContentDescription("Botão " + label);
        button.setOnTouchListener((view, event) -> {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN ||
                    action == MotionEvent.ACTION_POINTER_DOWN) {
                touchButtonsMask |= mask;
                nativeSetTouchButtons(touchButtonsMask);
                return true;
            }
            if (action == MotionEvent.ACTION_UP ||
                    action == MotionEvent.ACTION_POINTER_UP ||
                    action == MotionEvent.ACTION_CANCEL) {
                touchButtonsMask &= ~mask;
                nativeSetTouchButtons(touchButtonsMask);
                return true;
            }
            return true;
        });
        return button;
    }

    private TextView createMenuTouchButton(String label, int mask) {
        TextView button = createTouchButton(label, mask);
        button.setTextSize(10.0f);
        button.setBackground(createMenuTouchButtonBackground());
        LinearLayout.LayoutParams params =
                new LinearLayout.LayoutParams(dp(76), dp(34));
        params.setMargins(dp(5), 0, dp(5), 0);
        button.setLayoutParams(params);
        return button;
    }

    private GradientDrawable createTouchButtonBackground() {
        GradientDrawable background = new GradientDrawable();
        background.setShape(GradientDrawable.OVAL);
        background.setColor(0xAA171D26);
        background.setStroke(dp(1), 0xBFC6D0DC);
        return background;
    }

    private GradientDrawable createMenuTouchButtonBackground() {
        GradientDrawable background = new GradientDrawable();
        background.setColor(0xAA171D26);
        background.setCornerRadius(dp(10));
        background.setStroke(dp(1), 0xBFC6D0DC);
        return background;
    }

    private void clearVirtualPad() {
        touchButtonsMask = 0;
        nativeSetTouchButtons(0);
    }

    private GradientDrawable createStatusBackground(boolean isError) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(isError ? 0xE64A171B : 0xE6222730);
        background.setCornerRadius(dp(12));
        background.setStroke(dp(1), isError ? 0xFFFF8585 : 0xFF596574);
        return background;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    /**
     * Called by the Android native-app thread through JNI. UI changes are
     * always posted to the Activity's main thread.
     */
    public void onNativeRuntimeStatus(String message, boolean isError) {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) {
                return;
            }
            if (nativeStatusView == null) {
                pendingNativeStatus = message == null ? "" : message;
                pendingNativeStatusError = isError;
                return;
            }
            nativeStatusView.setText(message == null ? "" : message);
            nativeStatusView.setBackground(createStatusBackground(isError));
            nativeStatusView.setVisibility(View.VISIBLE);
        });
    }
}

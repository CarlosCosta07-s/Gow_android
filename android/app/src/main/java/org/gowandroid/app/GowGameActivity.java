package org.gowandroid.app;

import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
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
    private static native void nativeSetTouchAxes(
            float leftX, float leftY, float rightX, float rightY);

    private TextView nativeStatusView;
    private String pendingNativeStatus;
    private boolean pendingNativeStatusError;
    private int touchButtonsMask;
    private float touchLeftX, touchLeftY, touchRightX, touchRightY;
    private AnalogStickView leftStickView;
    private AnalogStickView rightStickView;

    private static final int PAD_LEFT = 0x0080;
    private static final int PAD_DOWN = 0x0040;
    private static final int PAD_RIGHT = 0x0020;
    private static final int PAD_UP = 0x0010;
    private static final int PAD_START = 0x0008;
    private static final int PAD_SELECT = 0x0001;
    private static final int PAD_L3 = 0x0002;
    private static final int PAD_R3 = 0x0004;
    private static final int PAD_SQUARE = 0x8000;
    private static final int PAD_CROSS = 0x4000;
    private static final int PAD_CIRCLE = 0x2000;
    private static final int PAD_TRIANGLE = 0x1000;
    private static final int PAD_L1 = 0x0400;
    private static final int PAD_R1 = 0x0800;
    private static final int PAD_L2 = 0x0100;
    private static final int PAD_R2 = 0x0200;

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

        // Add the PS2 control layer first so diagnostic/status widgets remain above it.
        createTouchControls(content);

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

    }

    private void createTouchControls(ViewGroup content) {
        FrameLayout controls = new FrameLayout(this);
        controls.setClipChildren(false);
        controls.setClipToPadding(false);
        controls.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
        content.addView(controls, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        // L2/L1 and R1/R2 sit on the top edges, like the DualShock 2 shoulders.
        LinearLayout leftShoulders = new LinearLayout(this);
        leftShoulders.setOrientation(LinearLayout.HORIZONTAL);
        leftShoulders.setGravity(Gravity.CENTER);
        leftShoulders.addView(createShoulderButton("L2", PAD_L2), shoulderCellLayout());
        leftShoulders.addView(createShoulderButton("L1", PAD_L1), shoulderCellLayout());
        FrameLayout.LayoutParams leftShoulderLayout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.START);
        leftShoulderLayout.setMargins(dp(12), dp(12), 0, 0);
        controls.addView(leftShoulders, leftShoulderLayout);

        LinearLayout rightShoulders = new LinearLayout(this);
        rightShoulders.setOrientation(LinearLayout.HORIZONTAL);
        rightShoulders.setGravity(Gravity.CENTER);
        rightShoulders.addView(createShoulderButton("R1", PAD_R1), shoulderCellLayout());
        rightShoulders.addView(createShoulderButton("R2", PAD_R2), shoulderCellLayout());
        FrameLayout.LayoutParams rightShoulderLayout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.END);
        rightShoulderLayout.setMargins(0, dp(12), dp(12), 0);
        controls.addView(rightShoulders, rightShoulderLayout);

        // Four-way digital cross, matching the PS2 D-pad.
        LinearLayout dpad = new LinearLayout(this);
        dpad.setOrientation(LinearLayout.VERTICAL);
        dpad.setGravity(Gravity.CENTER);
        dpad.setAlpha(0.82f);
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
        dpadLayout.setMargins(dp(8), 0, 0, dp(18));
        controls.addView(dpad, dpadLayout);

        // The two analog sticks stay independent and emit the same signed axes
        // consumed by the physical DualShock 2 packet encoder.
        leftStickView = new AnalogStickView(true);
        FrameLayout.LayoutParams leftStickLayout = new FrameLayout.LayoutParams(
                dp(108), dp(108), Gravity.BOTTOM | Gravity.START);
        leftStickLayout.setMargins(dp(154), 0, 0, dp(12));
        controls.addView(leftStickView, leftStickLayout);

        rightStickView = new AnalogStickView(false);
        FrameLayout.LayoutParams rightStickLayout = new FrameLayout.LayoutParams(
                dp(108), dp(108), Gravity.BOTTOM | Gravity.END);
        rightStickLayout.setMargins(0, 0, dp(154), dp(12));
        controls.addView(rightStickView, rightStickLayout);

        // L3/R3 are separate clickable stick buttons, as on the physical controller.
        TextView l3 = createMiniButton("L3", PAD_L3);
        FrameLayout.LayoutParams l3Layout = new FrameLayout.LayoutParams(
                dp(38), dp(28), Gravity.BOTTOM | Gravity.START);
        l3Layout.setMargins(dp(188), 0, 0, dp(116));
        controls.addView(l3, l3Layout);

        TextView r3 = createMiniButton("R3", PAD_R3);
        FrameLayout.LayoutParams r3Layout = new FrameLayout.LayoutParams(
                dp(38), dp(28), Gravity.BOTTOM | Gravity.END);
        r3Layout.setMargins(0, 0, dp(188), dp(116));
        controls.addView(r3, r3Layout);

        // Face buttons use the original PS2 diamond: Triangle up, Square left,
        // Circle right and Cross down.
        LinearLayout face = new LinearLayout(this);
        face.setOrientation(LinearLayout.VERTICAL);
        face.setGravity(Gravity.CENTER);
        face.setAlpha(0.84f);
        face.addView(createTouchRow(
                createTouchSpacer(), createTouchButton("△", PAD_TRIANGLE), createTouchSpacer()));
        face.addView(createTouchRow(
                createTouchButton("□", PAD_SQUARE), createTouchSpacer(),
                createTouchButton("○", PAD_CIRCLE)));
        face.addView(createTouchRow(
                createTouchSpacer(), createTouchButton("×", PAD_CROSS), createTouchSpacer()));
        FrameLayout.LayoutParams faceLayout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM | Gravity.END);
        faceLayout.setMargins(0, 0, dp(8), dp(18));
        controls.addView(face, faceLayout);

        LinearLayout menu = new LinearLayout(this);
        menu.setOrientation(LinearLayout.HORIZONTAL);
        menu.setGravity(Gravity.CENTER);
        menu.setAlpha(0.76f);
        menu.addView(createMenuTouchButton("SELECT", PAD_SELECT));
        menu.addView(createMenuTouchButton("START", PAD_START));
        FrameLayout.LayoutParams menuLayout = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        menuLayout.setMargins(0, 0, 0, dp(18));
        controls.addView(menu, menuLayout);
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

    private LinearLayout.LayoutParams touchCellLayout() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(46), dp(46));
        params.setMargins(dp(2), dp(2), dp(2), dp(2));
        return params;
    }

    private LinearLayout.LayoutParams shoulderCellLayout() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(58), dp(38));
        params.setMargins(dp(3), 0, dp(3), 0);
        return params;
    }

    private View createTouchSpacer() {
        return new View(this);
    }

    private TextView createTouchButton(String label, int mask) {
        TextView button = new TextView(this);
        button.setText(label);
        button.setTextColor(Color.WHITE);
        button.setTextSize(20.0f);
        button.setGravity(Gravity.CENTER);
        button.setBackground(createTouchButtonBackground());
        button.setContentDescription("Botão PS2 " + label);
        bindTouchButton(button, mask);
        return button;
    }

    private TextView createShoulderButton(String label, int mask) {
        TextView button = new TextView(this);
        button.setText(label);
        button.setTextColor(Color.WHITE);
        button.setTextSize(12.0f);
        button.setTypeface(null, android.graphics.Typeface.BOLD);
        button.setGravity(Gravity.CENTER);
        button.setBackground(createMenuTouchButtonBackground());
        button.setContentDescription("Botão PS2 " + label);
        bindTouchButton(button, mask);
        return button;
    }

    private TextView createMiniButton(String label, int mask) {
        TextView button = new TextView(this);
        button.setText(label);
        button.setTextColor(Color.WHITE);
        button.setTextSize(10.0f);
        button.setGravity(Gravity.CENTER);
        button.setBackground(createMenuTouchButtonBackground());
        button.setContentDescription("Botão PS2 " + label);
        bindTouchButton(button, mask);
        return button;
    }

    private void bindTouchButton(View button, int mask) {
        final int[] activePointers = {0};
        button.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN:
                    if (activePointers[0]++ == 0) {
                        touchButtonsMask |= mask;
                        nativeSetTouchButtons(touchButtonsMask);
                    }
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP:
                    if (activePointers[0] > 0 && --activePointers[0] == 0) {
                        touchButtonsMask &= ~mask;
                        nativeSetTouchButtons(touchButtonsMask);
                    }
                    return true;
                case MotionEvent.ACTION_CANCEL:
                    activePointers[0] = 0;
                    touchButtonsMask &= ~mask;
                    nativeSetTouchButtons(touchButtonsMask);
                    return true;
                default:
                    return true;
            }
        });
    }

    private TextView createMenuTouchButton(String label, int mask) {
        TextView button = createMiniButton(label, mask);
        button.setTextSize(10.0f);
        LinearLayout.LayoutParams params =
                new LinearLayout.LayoutParams(dp(72), dp(32));
        params.setMargins(dp(5), 0, dp(5), 0);
        button.setLayoutParams(params);
        return button;
    }

    private GradientDrawable createTouchButtonBackground() {
        GradientDrawable background = new GradientDrawable();
        background.setShape(GradientDrawable.OVAL);
        background.setColor(0x99171D26);
        background.setStroke(dp(1), 0xBFC6D0DC);
        return background;
    }

    private GradientDrawable createMenuTouchButtonBackground() {
        GradientDrawable background = new GradientDrawable();
        background.setColor(0x99171D26);
        background.setCornerRadius(dp(10));
        background.setStroke(dp(1), 0xBFC6D0DC);
        return background;
    }

    private void updateTouchAxes() {
        nativeSetTouchAxes(touchLeftX, touchLeftY, touchRightX, touchRightY);
    }

    private void setTouchStickAxes(boolean left, float x, float y) {
        if (left) {
            touchLeftX = x;
            touchLeftY = y;
        } else {
            touchRightX = x;
            touchRightY = y;
        }
        updateTouchAxes();
    }

    private void clearVirtualPad() {
        touchButtonsMask = 0;
        touchLeftX = touchLeftY = touchRightX = touchRightY = 0.0f;
        nativeSetTouchButtons(0);
        nativeSetTouchAxes(0.0f, 0.0f, 0.0f, 0.0f);
        if (leftStickView != null) leftStickView.releaseStick();
        if (rightStickView != null) rightStickView.releaseStick();
    }

    private final class AnalogStickView extends View {
        private final boolean leftStick;
        private final Paint outerPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint ringPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint knobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint labelPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private int activePointerId = MotionEvent.INVALID_POINTER_ID;
        private float axisX;
        private float axisY;
        private float centerX;
        private float centerY;
        private float travelRadius;

        AnalogStickView(boolean leftStick) {
            super(GowGameActivity.this);
            this.leftStick = leftStick;
            setClickable(true);
            outerPaint.setColor(0x66171D26);
            ringPaint.setColor(0xBFC6D0DC);
            ringPaint.setStyle(Paint.Style.STROKE);
            ringPaint.setStrokeWidth(dp(1.4f));
            knobPaint.setColor(0xCC303B49);
            knobPaint.setStyle(Paint.Style.FILL);
            labelPaint.setColor(Color.WHITE);
            labelPaint.setTextSize(dp(12));
            labelPaint.setTextAlign(Paint.Align.CENTER);
            labelPaint.setTypeface(android.graphics.Typeface.create(
                    "sans-serif", android.graphics.Typeface.BOLD));
            setContentDescription(leftStick ? "Analógico esquerdo PS2" : "Analógico direito PS2");
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            centerX = getWidth() * 0.5f;
            centerY = getHeight() * 0.48f;
            travelRadius = Math.min(getWidth(), getHeight()) * 0.31f;
            float outerRadius = travelRadius + dp(10);
            canvas.drawCircle(centerX, centerY, outerRadius, outerPaint);
            canvas.drawCircle(centerX, centerY, outerRadius, ringPaint);
            canvas.drawLine(centerX - travelRadius, centerY,
                    centerX + travelRadius, centerY, ringPaint);
            canvas.drawLine(centerX, centerY - travelRadius,
                    centerX, centerY + travelRadius, ringPaint);
            float knobX = centerX + axisX * travelRadius;
            float knobY = centerY + axisY * travelRadius;
            canvas.drawCircle(knobX, knobY, dp(22), knobPaint);
            canvas.drawCircle(knobX, knobY, dp(22), ringPaint);
            canvas.drawText(leftStick ? "L" : "R",
                    centerX, getHeight() - dp(2), labelPaint);
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN ||
                    action == MotionEvent.ACTION_POINTER_DOWN) {
                if (activePointerId == MotionEvent.INVALID_POINTER_ID) {
                    int index = event.getActionIndex();
                    activePointerId = event.getPointerId(index);
                    updateFromTouch(event.getX(index), event.getY(index));
                }
                return true;
            }
            if (action == MotionEvent.ACTION_MOVE) {
                int index = event.findPointerIndex(activePointerId);
                if (index >= 0) updateFromTouch(event.getX(index), event.getY(index));
                return true;
            }
            if (action == MotionEvent.ACTION_UP ||
                    action == MotionEvent.ACTION_POINTER_UP) {
                if (event.getPointerId(event.getActionIndex()) == activePointerId) {
                    releaseStick();
                }
                return true;
            }
            if (action == MotionEvent.ACTION_CANCEL) {
                releaseStick();
                return true;
            }
            return true;
        }

        private void updateFromTouch(float x, float y) {
            if (travelRadius <= 0.0f) {
                centerX = getWidth() * 0.5f;
                centerY = getHeight() * 0.48f;
                travelRadius = Math.min(getWidth(), getHeight()) * 0.31f;
            }
            float dx = (x - centerX) / Math.max(1.0f, travelRadius);
            float dy = (y - centerY) / Math.max(1.0f, travelRadius);
            float magnitude = (float) Math.sqrt(dx * dx + dy * dy);
            if (magnitude > 1.0f) {
                dx /= magnitude;
                dy /= magnitude;
            }
            axisX = Math.max(-1.0f, Math.min(1.0f, dx));
            axisY = Math.max(-1.0f, Math.min(1.0f, dy));
            setTouchStickAxes(leftStick, axisX, axisY);
            invalidate();
        }

        void releaseStick() {
            activePointerId = MotionEvent.INVALID_POINTER_ID;
            axisX = 0.0f;
            axisY = 0.0f;
            setTouchStickAxes(leftStick, 0.0f, 0.0f);
            invalidate();
        }
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

    private int dp(float value) {
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

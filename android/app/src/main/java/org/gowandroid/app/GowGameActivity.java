package org.gowandroid.app;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

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

    private static native boolean nativeSetDiscFileDescriptor(int descriptor);
    private static native void nativeSetActivityReference(GowGameActivity activity);
    private static native void nativeClearActivityReference(GowGameActivity activity);

    private TextView nativeStatusView;
    private String pendingNativeStatus;
    private boolean pendingNativeStatusError;

    static {
        System.loadLibrary("android-game");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        nativeSetActivityReference(this);
        createNativeStatusOverlay();

        int discDescriptor = getIntent().getIntExtra(EXTRA_DISC_FD, -1);
        if (discDescriptor >= 0 && !nativeSetDiscFileDescriptor(discDescriptor)) {
            android.util.Log.e(
                    "GowGameActivity",
                    "Could not initialize the native ISO reader from the supplied descriptor.");
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
    protected void onDestroy() {
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

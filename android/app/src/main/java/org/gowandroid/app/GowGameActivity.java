package org.gowandroid.app;

import android.os.Bundle;
import android.view.View;

import com.google.androidgamesdk.GameActivity;

/**
 * Native entry point reserved for the God of War runtime.
 *
 * The current native library is still an experimental Vulkan bring-up and is
 * not a playable game runtime. The standalone launcher action was removed;
 * this Activity will be connected to the integrated PS2 runtime when ready.
 */
public final class GowGameActivity extends GameActivity {
    static {
        System.loadLibrary("android-game");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

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
}

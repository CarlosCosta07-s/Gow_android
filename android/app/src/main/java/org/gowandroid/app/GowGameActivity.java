package org.gowandroid.app;

import android.os.Bundle;
import android.view.View;

import com.google.androidgamesdk.GameActivity;

/**
 * Native entry point for the Gow_android Vulkan bring-up shell.
 *
 * The native library contains GameActivity's native glue and the application
 * entry point. The initial milestone only validates Android window/surface
 * presentation; it does not execute God of War yet.
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

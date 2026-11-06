package org.gowandroid.app;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.Build;
import android.text.format.DateFormat;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Tracks the wall clock and the battery while the launcher is visible.
 *
 * It listens only to protected system broadcasts, needs no permission, and
 * does nothing while stopped: call {@link #start()} from onStart and
 * {@link #stop()} from onStop. Callbacks arrive on the main thread, and both
 * are delivered once from {@link #start()} so the UI is never stale after the
 * app was in the background.
 */
final class DeviceStatusMonitor {
    interface Listener {
        void onTimeChanged(long nowMillis);

        void onBatteryChanged(BatteryReading reading);
    }

    private final Context context;
    private final Listener listener;
    private boolean started;

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context receiverContext, Intent intent) {
            if (intent == null) {
                return;
            }
            String action = intent.getAction();
            if (Intent.ACTION_BATTERY_CHANGED.equals(action)) {
                listener.onBatteryChanged(readBattery(intent));
            } else if (Intent.ACTION_TIME_TICK.equals(action)
                    || Intent.ACTION_TIME_CHANGED.equals(action)
                    || Intent.ACTION_TIMEZONE_CHANGED.equals(action)) {
                listener.onTimeChanged(System.currentTimeMillis());
            }
        }
    };

    DeviceStatusMonitor(Context context, Listener listener) {
        Context applicationContext = context.getApplicationContext();
        this.context = applicationContext != null ? applicationContext : context;
        this.listener = listener;
    }

    void start() {
        if (started) {
            return;
        }
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_BATTERY_CHANGED);
        filter.addAction(Intent.ACTION_TIME_TICK);
        filter.addAction(Intent.ACTION_TIME_CHANGED);
        filter.addAction(Intent.ACTION_TIMEZONE_CHANGED);

        // ACTION_BATTERY_CHANGED is sticky: registering returns its last value.
        Intent sticky;
        if (Build.VERSION.SDK_INT >= 33) {
            sticky = context.registerReceiver(
                    receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            sticky = context.registerReceiver(receiver, filter);
        }
        started = true;

        listener.onTimeChanged(System.currentTimeMillis());
        listener.onBatteryChanged(
                sticky != null ? readBattery(sticky) : BatteryReading.UNKNOWN);
    }

    void stop() {
        if (!started) {
            return;
        }
        started = false;
        try {
            context.unregisterReceiver(receiver);
        } catch (IllegalArgumentException ignored) {
            // Already unregistered.
        }
    }

    /** Time of day honouring the user's 12/24-hour setting and locale. */
    static String formatTime(Context context, long millis) {
        return DateFormat.getTimeFormat(context).format(new Date(millis));
    }

    /** Short weekday and date in the user's locale, e.g. "qui., 6 de nov.". */
    static String formatDate(long millis) {
        Locale locale = Locale.getDefault();
        String pattern = DateFormat.getBestDateTimePattern(locale, "EEEdMMM");
        return new SimpleDateFormat(pattern, locale).format(new Date(millis));
    }

    private static BatteryReading readBattery(Intent intent) {
        return BatteryReading.from(
                intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1),
                intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1),
                intent.getIntExtra(BatteryManager.EXTRA_STATUS,
                        BatteryManager.BATTERY_STATUS_UNKNOWN),
                intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0),
                intent.getBooleanExtra(BatteryManager.EXTRA_PRESENT, true));
    }
}

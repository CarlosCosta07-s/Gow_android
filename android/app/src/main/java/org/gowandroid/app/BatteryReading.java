package org.gowandroid.app;

/**
 * Immutable, Android-free interpretation of one battery reading.
 *
 * The integer constants mirror {@code android.os.BatteryManager} so this class
 * can be exercised by a plain JVM test; {@link DeviceStatusMonitor} passes the
 * raw ACTION_BATTERY_CHANGED extras straight through.
 */
final class BatteryReading {
    enum Severity { NONE, LOW, CRITICAL }

    // android.os.BatteryManager.BATTERY_STATUS_*
    static final int STATUS_UNKNOWN = 1;
    static final int STATUS_CHARGING = 2;
    static final int STATUS_DISCHARGING = 3;
    static final int STATUS_NOT_CHARGING = 4;
    static final int STATUS_FULL = 5;

    // android.os.BatteryManager.BATTERY_PLUGGED_*
    static final int PLUGGED_AC = 1;
    static final int PLUGGED_USB = 2;
    static final int PLUGGED_WIRELESS = 4;
    static final int PLUGGED_DOCK = 8;

    // Mirrors Android's default low-battery and critical thresholds.
    static final int LOW_PERCENT = 15;
    static final int CRITICAL_PERCENT = 5;

    static final BatteryReading UNKNOWN =
            new BatteryReading(-1, STATUS_UNKNOWN, 0);

    private final int percent;
    private final int status;
    private final int plugged;

    private BatteryReading(int percent, int status, int plugged) {
        this.percent = percent;
        this.status = status;
        this.plugged = plugged;
    }

    /**
     * @param level   EXTRA_LEVEL
     * @param scale   EXTRA_SCALE
     * @param status  EXTRA_STATUS
     * @param plugged EXTRA_PLUGGED (bit mask, 0 when on battery)
     * @param present EXTRA_PRESENT
     */
    static BatteryReading from(int level, int scale, int status,
                               int plugged, boolean present) {
        if (!present || level < 0 || scale <= 0) {
            return UNKNOWN;
        }
        long scaled = ((long) level * 100L + scale / 2L) / scale;
        int percent = (int) Math.max(0L, Math.min(100L, scaled));
        int normalizedStatus = status >= STATUS_UNKNOWN && status <= STATUS_FULL
                ? status : STATUS_UNKNOWN;
        return new BatteryReading(percent, normalizedStatus, Math.max(0, plugged));
    }

    boolean isKnown() {
        return percent >= 0;
    }

    /** Charge level in 0..100, or -1 when unknown. */
    int percent() {
        return percent;
    }

    boolean isCharging() {
        return isKnown() && status == STATUS_CHARGING;
    }

    boolean isFull() {
        return isKnown() && status == STATUS_FULL;
    }

    boolean isPluggedIn() {
        return isKnown() && plugged != 0;
    }

    /** Warning level; never raised while a power source is connected. */
    Severity severity() {
        if (!isKnown() || isPluggedIn()) {
            return Severity.NONE;
        }
        if (percent <= CRITICAL_PERCENT) {
            return Severity.CRITICAL;
        }
        if (percent <= LOW_PERCENT) {
            return Severity.LOW;
        }
        return Severity.NONE;
    }

    String percentText() {
        return isKnown() ? percent + "%" : "--";
    }

    /** Short state for a compact chip. */
    String stateText() {
        if (!isKnown()) {
            return "Indisponível";
        }
        if (status == STATUS_FULL) {
            return "Carga completa";
        }
        if (status == STATUS_CHARGING) {
            return "Carregando";
        }
        return isPluggedIn() ? "Conectado" : "Na bateria";
    }

    /** Full sentence for screen readers. */
    String accessibilityText() {
        if (!isKnown()) {
            return "Bateria indisponível";
        }
        StringBuilder text = new StringBuilder("Bateria ")
                .append(percent).append(" por cento");
        if (status == STATUS_FULL) {
            text.append(", carga completa");
        } else if (status == STATUS_CHARGING) {
            text.append(", carregando");
            String source = sourceText();
            if (source != null) {
                text.append(' ').append(source);
            }
        } else if (isPluggedIn()) {
            text.append(", conectada, sem carregar");
        } else {
            text.append(", descarregando");
        }
        switch (severity()) {
            case CRITICAL:
                text.append(", nível crítico");
                break;
            case LOW:
                text.append(", nível baixo");
                break;
            default:
                break;
        }
        return text.toString();
    }

    private String sourceText() {
        if ((plugged & PLUGGED_WIRELESS) != 0) return "sem fio";
        if ((plugged & PLUGGED_USB) != 0) return "por USB";
        if ((plugged & PLUGGED_AC) != 0) return "na tomada";
        if ((plugged & PLUGGED_DOCK) != 0) return "na base";
        return null;
    }
}

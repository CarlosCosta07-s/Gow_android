package org.gowandroid.app;

/**
 * Plain-JVM test for BatteryReading (no Android, no JUnit):
 *   javac -d out BatteryReading.java BatteryReadingTest.java
 *   java -cp out org.gowandroid.app.BatteryReadingTest
 */
public final class BatteryReadingTest {
    private static int failures;

    private static void check(boolean condition, String message) {
        if (!condition) {
            failures++;
            System.err.println("FAIL: " + message);
        }
    }

    private static void eq(Object expected, Object actual, String message) {
        check(expected.equals(actual),
                message + " (expected <" + expected + ">, got <" + actual + ">)");
    }

    private static BatteryReading read(int level, int scale, int status, int plugged) {
        return BatteryReading.from(level, scale, status, plugged, true);
    }

    public static void main(String[] args) {
        // Percentage maths.
        eq(87, read(87, 100, 3, 0).percent(), "level/scale 87/100");
        eq(59, read(150, 255, 3, 0).percent(), "non-100 scale rounds to nearest");
        eq(0, read(0, 100, 3, 0).percent(), "empty battery is 0, not unknown");
        eq(100, read(250, 100, 3, 0).percent(), "level above scale clamps to 100");
        eq(100, read(Integer.MAX_VALUE, Integer.MAX_VALUE, 3, 0).percent(),
                "large values do not overflow");

        // Unknown inputs.
        check(!read(-1, 100, 3, 0).isKnown(), "negative level is unknown");
        check(!read(50, 0, 3, 0).isKnown(), "zero scale is unknown");
        check(!read(50, -5, 3, 0).isKnown(), "negative scale is unknown");
        check(!BatteryReading.from(50, 100, 3, 0, false).isKnown(),
                "absent battery is unknown");
        eq("--", BatteryReading.UNKNOWN.percentText(), "unknown percent text");
        eq("Indisponível", BatteryReading.UNKNOWN.stateText(), "unknown state text");
        eq("Bateria indisponível", BatteryReading.UNKNOWN.accessibilityText(),
                "unknown accessibility text");
        eq(BatteryReading.Severity.NONE, BatteryReading.UNKNOWN.severity(),
                "unknown never warns");

        // State.
        BatteryReading onBattery = read(87, 100, 3, 0);
        check(!onBattery.isCharging() && !onBattery.isFull() && !onBattery.isPluggedIn(),
                "on battery flags");
        eq("87%", onBattery.percentText(), "percent text");
        eq("Na bateria", onBattery.stateText(), "on battery text");

        BatteryReading charging = read(40, 100, 2, BatteryReading.PLUGGED_USB);
        check(charging.isCharging() && charging.isPluggedIn(), "charging flags");
        eq("Carregando", charging.stateText(), "charging text");

        eq("Carga completa", read(100, 100, 5, 1).stateText(), "full text");
        check(read(100, 100, 5, 1).isFull(), "full flag");

        BatteryReading idle = read(60, 100, 4, BatteryReading.PLUGGED_AC);
        check(!idle.isCharging() && idle.isPluggedIn(), "plugged but not charging flags");
        eq("Conectado", idle.stateText(), "plugged but not charging text");

        eq("Na bateria", read(60, 100, 99, 0).stateText(),
                "out-of-range status falls back to unknown status");
        eq("Conectado", read(60, 100, 99, 2).stateText(),
                "unknown status while plugged reads as connected");

        // Severity thresholds.
        eq(BatteryReading.Severity.NONE, read(16, 100, 3, 0).severity(), "16% is fine");
        eq(BatteryReading.Severity.LOW, read(15, 100, 3, 0).severity(), "15% is low");
        eq(BatteryReading.Severity.LOW, read(6, 100, 3, 0).severity(), "6% is low");
        eq(BatteryReading.Severity.CRITICAL, read(5, 100, 3, 0).severity(), "5% is critical");
        eq(BatteryReading.Severity.CRITICAL, read(0, 100, 3, 0).severity(), "0% is critical");
        eq(BatteryReading.Severity.NONE, read(3, 100, 2, 2).severity(),
                "charging suppresses the warning");
        eq(BatteryReading.Severity.NONE, read(3, 100, 4, 1).severity(),
                "any power source suppresses the warning");

        // Accessibility text.
        eq("Bateria 87 por cento, descarregando",
                onBattery.accessibilityText(), "a11y on battery");
        eq("Bateria 40 por cento, carregando por USB",
                charging.accessibilityText(), "a11y charging via USB");
        eq("Bateria 40 por cento, carregando na tomada",
                read(40, 100, 2, 1).accessibilityText(), "a11y charging via AC");
        eq("Bateria 40 por cento, carregando sem fio",
                read(40, 100, 2, 4).accessibilityText(), "a11y charging wireless");
        eq("Bateria 40 por cento, carregando na base",
                read(40, 100, 2, 8).accessibilityText(), "a11y charging dock");
        eq("Bateria 40 por cento, carregando",
                read(40, 100, 2, 0).accessibilityText(), "a11y charging without source");
        eq("Bateria 100 por cento, carga completa",
                read(100, 100, 5, 0).accessibilityText(), "a11y full");
        eq("Bateria 60 por cento, conectada, sem carregar",
                idle.accessibilityText(), "a11y plugged idle");
        eq("Bateria 12 por cento, descarregando, nível baixo",
                read(12, 100, 3, 0).accessibilityText(), "a11y low");
        eq("Bateria 4 por cento, descarregando, nível crítico",
                read(4, 100, 3, 0).accessibilityText(), "a11y critical");

        if (failures > 0) {
            System.err.println(failures + " failure(s)");
            System.exit(1);
        }
        System.out.println("BatteryReading tests passed");
    }
}

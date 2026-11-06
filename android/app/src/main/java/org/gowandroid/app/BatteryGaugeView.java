package org.gowandroid.app;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.View;

/**
 * Small battery glyph drawn with Canvas, so it renders identically on every
 * device (no emoji or icon-font dependence). Draws a level bar coloured by
 * severity, and a bolt while charging.
 */
final class BatteryGaugeView extends View {
    private static final int COLOR_OUTLINE = 0xFF969DA7;
    private static final int COLOR_OK = 0xFF8BC28F;
    private static final int COLOR_LOW = 0xFFE0B04A;
    private static final int COLOR_CRITICAL = 0xFFE27676;
    private static final int COLOR_BOLT = 0xFFF2F1EF;

    // Lightning bolt in a unit box (x right, y down).
    private static final float[] BOLT = {
            0.62f, 0.00f, 0.12f, 0.58f, 0.46f, 0.58f,
            0.34f, 1.00f, 0.88f, 0.40f, 0.52f, 0.40f
    };

    private static final float DEFAULT_WIDTH_DP = 26f;
    private static final float DEFAULT_HEIGHT_DP = 13f;

    private final float density;
    private final Paint outlinePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF body = new RectF();
    private final RectF cap = new RectF();
    private final RectF level = new RectF();
    private final Path bolt = new Path();

    private BatteryReading reading = BatteryReading.UNKNOWN;

    BatteryGaugeView(Context context) {
        super(context);
        density = getResources().getDisplayMetrics().density;
        outlinePaint.setStyle(Paint.Style.STROKE);
        outlinePaint.setStrokeWidth(Math.max(1f, 1.2f * density));
        outlinePaint.setColor(COLOR_OUTLINE);
        fillPaint.setStyle(Paint.Style.FILL);
        setReading(BatteryReading.UNKNOWN);
    }

    void setReading(BatteryReading newReading) {
        reading = newReading != null ? newReading : BatteryReading.UNKNOWN;
        setContentDescription(reading.accessibilityText());
        invalidate();
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        setMeasuredDimension(
                resolveSize(Math.round(DEFAULT_WIDTH_DP * density), widthMeasureSpec),
                resolveSize(Math.round(DEFAULT_HEIGHT_DP * density), heightMeasureSpec));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        final float width = getWidth();
        final float height = getHeight();
        if (width <= 0f || height <= 0f) {
            return;
        }

        final float stroke = outlinePaint.getStrokeWidth();
        final float capWidth = Math.max(2f, width * 0.09f);
        final float half = stroke / 2f;
        body.set(half, half, width - capWidth - half, height - half);
        final float radius = body.height() * 0.2f;

        cap.set(width - capWidth, height * 0.3f, width, height * 0.7f);
        fillPaint.setColor(COLOR_OUTLINE);
        canvas.drawRoundRect(cap, capWidth / 2f, capWidth / 2f, fillPaint);
        canvas.drawRoundRect(body, radius, radius, outlinePaint);

        if (!reading.isKnown()) {
            return;
        }

        final float pad = stroke + density * 0.8f;
        final float innerLeft = body.left + pad;
        final float innerRight = body.right - pad;
        final float innerWidth = innerRight - innerLeft;
        if (innerWidth > 0f && reading.percent() > 0) {
            final float fillWidth = Math.max(
                    density * 1.5f, innerWidth * reading.percent() / 100f);
            level.set(innerLeft, body.top + pad, innerLeft + Math.min(fillWidth, innerWidth),
                    body.bottom - pad);
            fillPaint.setColor(fillColor());
            final float innerRadius = Math.max(0f, radius - pad);
            canvas.drawRoundRect(level, innerRadius, innerRadius, fillPaint);
        }

        if (reading.isCharging()) {
            final float boltHeight = body.height() * 0.78f;
            final float boltWidth = boltHeight * 0.6f;
            final float left = body.centerX() - boltWidth / 2f;
            final float top = body.centerY() - boltHeight / 2f;
            bolt.reset();
            for (int i = 0; i < BOLT.length; i += 2) {
                final float x = left + BOLT[i] * boltWidth;
                final float y = top + BOLT[i + 1] * boltHeight;
                if (i == 0) {
                    bolt.moveTo(x, y);
                } else {
                    bolt.lineTo(x, y);
                }
            }
            bolt.close();
            fillPaint.setColor(COLOR_BOLT);
            canvas.drawPath(bolt, fillPaint);
        }
    }

    private int fillColor() {
        switch (reading.severity()) {
            case CRITICAL:
                return COLOR_CRITICAL;
            case LOW:
                return COLOR_LOW;
            default:
                return COLOR_OK;
        }
    }
}

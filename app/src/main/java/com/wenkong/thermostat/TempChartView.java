package com.wenkong.thermostat;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.View;
import java.util.LinkedList;

/**
 * 温度曲线图 (无外部依赖, 纯 Canvas 绘制)
 * 显示最近 N 个温度数据点，误差 > 0.5° 用红色，否则绿色
 */
public class TempChartView extends View {

    private static final int MAX_POINTS = 120;

    private final LinkedList<Float> temps = new LinkedList<>();
    private final LinkedList<Float> errors = new LinkedList<>(); // 误差数据，用于颜色标识
    private final Paint greenPaint, redPaint, gridPaint, textPaint, setpointPaint;
    private float minTemp = 20, maxTemp = 50;
    private float currentSetpoint = 0;
    private int lastMode = 0; // 0=PID, 1=其他

    public TempChartView(Context context, AttributeSet attrs) {
        super(context, attrs);

        greenPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        greenPaint.setColor(0xFF4CAF50);
        greenPaint.setStrokeWidth(3f);
        greenPaint.setStyle(Paint.Style.STROKE);

        redPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        redPaint.setColor(0xFFF44336);
        redPaint.setStrokeWidth(3f);
        redPaint.setStyle(Paint.Style.STROKE);

        setpointPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        setpointPaint.setColor(0xFFF4511E);
        setpointPaint.setStrokeWidth(2f);
        setpointPaint.setStyle(Paint.Style.STROKE);

        gridPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        gridPaint.setColor(0xFFB0BEC5);
        gridPaint.setStrokeWidth(1f);

        textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        textPaint.setColor(0xFF78909C);
        textPaint.setTextSize(24f);
    }

    /** 添加一个数据点 */
    public void addTemp(float temp, float setpoint, String mode) {
        temps.addLast(temp);
        float error = setpoint - temp;
        errors.addLast(error);
        currentSetpoint = setpoint;
        lastMode = "PID".equals(mode) ? 0 : 1;

        if (temps.size() > MAX_POINTS) {
            temps.removeFirst();
            errors.removeFirst();
        }

        // 自动调整 Y 轴范围
        float m = Float.MAX_VALUE, M = Float.MIN_VALUE;
        for (float t : temps) {
            if (t < m) m = t; if (t > M) M = t;
        }
        // 同时考虑设定温度，确保其可见
        if (setpoint < m) m = setpoint;
        if (setpoint > M) M = setpoint;
        if (M - m < 5) { m -= 1; M += 1; }
        minTemp = m; maxTemp = M;
        postInvalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth(), h = getHeight();
        if (w < 10 || h < 10 || temps.isEmpty()) return;

        float padL = 55, padR = 10, padT = 10, padB = 30;
        float chartW = w - padL - padR;
        float chartH = h - padT - padB;
        float range = maxTemp - minTemp;
        if (range < 0.1f) range = 1;

        // 网格线 + 纵轴标签（显示一位小数）
        for (int i = 0; i <= 4; i++) {
            float y = padT + chartH * i / 4;
            canvas.drawLine(padL, y, w - padR, y, gridPaint);
            float tempVal = maxTemp - range * i / 4;
            canvas.drawText(String.format("%.1f", tempVal), 4, y + 8, textPaint);
        }

        // 设定温度虚线
        float spY = padT + chartH * (maxTemp - currentSetpoint) / range;
        if (spY >= padT && spY <= padT + chartH) {
            canvas.drawLine(padL, spY, w - padR, spY, setpointPaint);
            canvas.drawText("SP", w - padR - 25, spY - 4, textPaint);
        }

        int n = temps.size();
        float stepX = chartW / MAX_POINTS;

        // PID 模式：按误差分色绘制线段
        if (lastMode == 0) {
            // 先画绿色段（误差 <= 0.5）
            drawSegments(canvas, padL, padT, chartH, chartW, range, stepX, n, 0.5f, false);
            // 再画红色段（误差 > 0.5）
            drawSegments(canvas, padL, padT, chartH, chartW, range, stepX, n, 0.5f, true);
        } else {
            // 非 PID 模式：统一深橙色
            Path path = new Path();
            for (int i = 0; i < n; i++) {
                float x = padL + chartW - (n - i) * stepX;
                float y = padT + chartH * (maxTemp - temps.get(i)) / range;
                if (i == 0) path.moveTo(x, y);
                else path.lineTo(x, y);
            }
            canvas.drawPath(path, greenPaint);
        }
    }

    private void drawSegments(Canvas canvas, float padL, float padT, float chartH,
                               float chartW, float range, float stepX, int n,
                               float threshold, boolean drawError) {
        Path path = new Path();
        boolean inSegment = false;

        for (int i = 0; i < n; i++) {
            boolean isError = Math.abs(errors.get(i)) > threshold;
            if (isError != drawError) {
                inSegment = false;
                continue;
            }
            float x = padL + chartW - (n - i) * stepX;
            float y = padT + chartH * (maxTemp - temps.get(i)) / range;
            if (!inSegment) {
                path.moveTo(x, y);
                inSegment = true;
            } else {
                path.lineTo(x, y);
            }
        }
        if (!path.isEmpty()) {
            canvas.drawPath(path, drawError ? redPaint : greenPaint);
        }
    }
}

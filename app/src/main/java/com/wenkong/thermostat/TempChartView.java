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
 * 显示最近 N 个温度数据点
 */
public class TempChartView extends View {

    private static final int MAX_POINTS = 120;  // 60s @ 0.5Hz
    private final LinkedList<Float> data = new LinkedList<>();
    private final Paint linePaint, gridPaint, textPaint;
    private float minTemp = 20, maxTemp = 50;   // 显示范围

    public TempChartView(Context context, AttributeSet attrs) {
        super(context, attrs);

        linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        linePaint.setColor(0xFFF4511E);   // 深橙色
        linePaint.setStrokeWidth(3f);
        linePaint.setStyle(Paint.Style.STROKE);

        gridPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        gridPaint.setColor(0xFFB0BEC5);
        gridPaint.setStrokeWidth(1f);

        textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        textPaint.setColor(0xFF78909C);
        textPaint.setTextSize(24f);
    }

    /** 添加一个温度数据点 */
    public void addTemp(float temp) {
        data.addLast(temp);
        if (data.size() > MAX_POINTS) data.removeFirst();

        // 自动调整 Y 轴范围
        float m = Float.MAX_VALUE, M = Float.MIN_VALUE;
        for (float t : data) {
            if (t < m) m = t; if (t > M) M = t;
        }
        if (M - m < 5) { m -= 1; M += 1; }  // 至少 ±1°C 范围
        minTemp = m; maxTemp = M;
        postInvalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth(), h = getHeight();
        if (w < 10 || h < 10 || data.isEmpty()) return;

        float padL = 50, padR = 10, padT = 10, padB = 30;
        float chartW = w - padL - padR;
        float chartH = h - padT - padB;
        float range = maxTemp - minTemp;
        if (range < 0.1f) range = 1;

        // 网格线 + 纵轴标签
        for (int i = 0; i <= 4; i++) {
            float y = padT + chartH * i / 4;
            canvas.drawLine(padL, y, w - padR, y, gridPaint);

            float tempVal = maxTemp - range * i / 4;
            canvas.drawText(String.format("%.0f", tempVal), 4, y + 8, textPaint);
        }

        // 温度曲线
        Path path = new Path();
        int n = data.size();
        float stepX = chartW / MAX_POINTS;  // 固定间距, 滚动显示

        for (int i = 0; i < n; i++) {
            float x = padL + chartW - (n - i) * stepX;
            float y = padT + chartH * (maxTemp - data.get(i)) / range;
            if (i == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        canvas.drawPath(path, linePaint);
    }
}

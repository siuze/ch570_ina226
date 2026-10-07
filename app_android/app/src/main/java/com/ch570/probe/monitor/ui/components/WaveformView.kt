package com.ch570.probe.monitor.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ch570.probe.monitor.model.TelemetryPoint
import com.ch570.probe.monitor.ui.theme.ColorBgCard
import com.ch570.probe.monitor.ui.theme.ColorBorder
import com.ch570.probe.monitor.ui.theme.ColorCurrent
import com.ch570.probe.monitor.ui.theme.ColorGridLine
import com.ch570.probe.monitor.ui.theme.ColorPower
import com.ch570.probe.monitor.ui.theme.ColorTextMuted
import com.ch570.probe.monitor.ui.theme.ColorVoltage
import java.util.Locale
import kotlin.math.max

@Composable
fun WaveformView(
    points: List<TelemetryPoint>,
    timeWindowSeconds: Double,
    showCurrent: Boolean,
    showPower: Boolean,
    showVoltage: Boolean,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(260.dp)
            .shadow(3.dp, RoundedCornerShape(8.dp))
            .background(ColorBgCard, RoundedCornerShape(8.dp))
            .border(1.dp, ColorBorder, RoundedCornerShape(8.dp))
            .padding(8.dp)
    ) {
        Column(modifier = Modifier.fillMaxSize()) {
            Canvas(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
            ) {
                val canvasWidth = size.width
                val canvasHeight = size.height

                // Draw background grid lines (horizontal 4 segments, vertical 5 segments)
                val hLines = 4
                for (i in 0..hLines) {
                    val y = canvasHeight * i / hLines
                    drawLine(
                        color = ColorGridLine,
                        start = Offset(0f, y),
                        end = Offset(canvasWidth, y),
                        strokeWidth = 1f
                    )
                }

                val vLines = 5
                for (i in 0..vLines) {
                    val x = canvasWidth * i / vLines
                    drawLine(
                        color = ColorGridLine,
                        start = Offset(x, 0f),
                        end = Offset(x, canvasHeight),
                        strokeWidth = 1f
                    )
                }

                if (points.isEmpty()) return@Canvas

                val latestTime = points.last().timestamp
                val startTime = latestTime - timeWindowSeconds

                // Compute Y range for enabled channels
                var minI = Double.MAX_VALUE
                var maxI = -Double.MAX_VALUE
                var minP = Double.MAX_VALUE
                var maxP = -Double.MAX_VALUE
                var minV = Double.MAX_VALUE
                var maxV = -Double.MAX_VALUE

                for (p in points) {
                    if (p.currentMa < minI) minI = p.currentMa
                    if (p.currentMa > maxI) maxI = p.currentMa
                    if (p.powerMw < minP) minP = p.powerMw
                    if (p.powerMw > maxP) maxP = p.powerMw
                    if (p.voltageV < minV) minV = p.voltageV
                    if (p.voltageV > maxV) maxV = p.voltageV
                }

                // Add margin to Y ranges
                val rangeI = max(10.0, (maxI - minI) * 1.2)
                val baseMinI = minI - rangeI * 0.1
                val rangeP = max(20.0, (maxP - minP) * 1.2)
                val baseMinP = minP - rangeP * 0.1
                val rangeV = max(0.5, (maxV - minV) * 1.2)
                val baseMinV = minV - rangeV * 0.1

                fun mapX(t: Double): Float {
                    val fraction = (t - startTime) / timeWindowSeconds
                    return (fraction * canvasWidth).toFloat().coerceIn(0f, canvasWidth)
                }

                fun mapY(valInput: Double, baseMin: Double, range: Double): Float {
                    val fraction = (valInput - baseMin) / range
                    return (canvasHeight - (fraction * canvasHeight)).toFloat().coerceIn(0f, canvasHeight)
                }

                // Draw Current Curve (Green)
                if (showCurrent && points.isNotEmpty()) {
                    val pathI = Path()
                    var first = true
                    for (p in points) {
                        val x = mapX(p.timestamp)
                        val y = mapY(p.currentMa, baseMinI, rangeI)
                        if (first) {
                            pathI.moveTo(x, y)
                            first = false
                        } else {
                            pathI.lineTo(x, y)
                        }
                    }
                    drawPath(
                        path = pathI,
                        color = ColorCurrent,
                        style = Stroke(width = 2.dp.toPx())
                    )
                }

                // Draw Power Curve (Amber)
                if (showPower && points.isNotEmpty()) {
                    val pathP = Path()
                    var first = true
                    for (p in points) {
                        val x = mapX(p.timestamp)
                        val y = mapY(p.powerMw, baseMinP, rangeP)
                        if (first) {
                            pathP.moveTo(x, y)
                            first = false
                        } else {
                            pathP.lineTo(x, y)
                        }
                    }
                    drawPath(
                        path = pathP,
                        color = ColorPower,
                        style = Stroke(width = 2.dp.toPx())
                    )
                }

                // Draw Voltage Curve (Sky Blue)
                if (showVoltage && points.isNotEmpty()) {
                    val pathV = Path()
                    var first = true
                    for (p in points) {
                        val x = mapX(p.timestamp)
                        val y = mapY(p.voltageV, baseMinV, rangeV)
                        if (first) {
                            pathV.moveTo(x, y)
                            first = false
                        } else {
                            pathV.lineTo(x, y)
                        }
                    }
                    drawPath(
                        path = pathV,
                        color = ColorVoltage,
                        style = Stroke(width = 2.dp.toPx())
                    )
                }
            }

            // Fixed Stationary Time Axis label underneath (e.g. 0s --------- 15s --------- 30s)
            androidx.compose.foundation.layout.Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 4.dp),
                horizontalArrangement = androidx.compose.foundation.layout.Arrangement.SpaceBetween
            ) {
                Text(
                    text = String.format(Locale.US, "-%.0fs", timeWindowSeconds),
                    fontSize = 10.sp,
                    color = ColorTextMuted
                )
                Text(
                    text = String.format(Locale.US, "-%.0fs", timeWindowSeconds / 2.0),
                    fontSize = 10.sp,
                    color = ColorTextMuted
                )
                Text(
                    text = "0s (当前)",
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    color = ColorTextMuted
                )
            }
        }
    }
}

package com.ch570.probe.monitor.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ch570.probe.monitor.model.TelemetryPoint
import com.ch570.probe.monitor.ui.theme.ColorBgCard
import com.ch570.probe.monitor.ui.theme.ColorBorder
import com.ch570.probe.monitor.ui.theme.ColorCurrent
import com.ch570.probe.monitor.ui.theme.ColorGridLine
import com.ch570.probe.monitor.ui.theme.ColorPower
import com.ch570.probe.monitor.ui.theme.ColorTextMain
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
    selectedPoint: TelemetryPoint?,
    onSelectPoint: (Double) -> Unit,
    onClearSelection: () -> Unit,
    modifier: Modifier = Modifier
) {
    val textMeasurer = rememberTextMeasurer()

    // Interactive Y-axis pan and zoom scale
    var yZoomScale by remember { mutableFloatStateOf(1.0f) }
    var yPanOffset by remember { mutableFloatStateOf(0.0f) }

    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(290.dp)
            .shadow(3.dp, RoundedCornerShape(8.dp))
            .background(ColorBgCard, RoundedCornerShape(8.dp))
            .border(1.dp, ColorBorder, RoundedCornerShape(8.dp))
            .padding(top = 8.dp, bottom = 6.dp, start = 6.dp, end = 10.dp)
    ) {
        Column(modifier = Modifier.fillMaxSize()) {
            Canvas(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .pointerInput(Unit) {
                        detectTransformGestures { _, pan, zoom, _ ->
                            // Vertical pan & zoom on Y axis
                            yZoomScale = (yZoomScale * zoom).coerceIn(0.2f, 10.0f)
                            yPanOffset += pan.y
                        }
                    }
                    .pointerInput(points, timeWindowSeconds) {
                        detectTapGestures(
                            onTap = { offset ->
                                val yAxisWidth = 48.dp.toPx()
                                val plotWidth = size.width - yAxisWidth
                                if (offset.x >= yAxisWidth && plotWidth > 0 && points.isNotEmpty()) {
                                    val latestTime = points.last().timestamp
                                    val startTime = latestTime - timeWindowSeconds
                                    val fraction = ((offset.x - yAxisWidth) / plotWidth).coerceIn(0f, 1f)
                                    val targetTime = startTime + fraction * timeWindowSeconds
                                    onSelectPoint(targetTime)
                                }
                            },
                            onDoubleTap = {
                                onClearSelection()
                                yZoomScale = 1.0f
                                yPanOffset = 0.0f
                            }
                        )
                    }
            ) {
                val totalWidth = size.width
                val totalHeight = size.height

                val yAxisWidth = 48.dp.toPx()
                val plotLeft = yAxisWidth
                val plotWidth = totalWidth - yAxisWidth
                val plotHeight = totalHeight

                // Draw background grid lines (4 horizontal, 5 vertical)
                val hLines = 4
                for (i in 0..hLines) {
                    val y = plotHeight * i / hLines
                    drawLine(
                        color = ColorGridLine,
                        start = Offset(plotLeft, y),
                        end = Offset(totalWidth, y),
                        strokeWidth = 1f
                    )
                }

                val vLines = 5
                for (i in 0..vLines) {
                    val x = plotLeft + plotWidth * i / vLines
                    drawLine(
                        color = ColorGridLine,
                        start = Offset(x, 0f),
                        end = Offset(x, plotHeight),
                        strokeWidth = 1f
                    )
                }

                // Y-axis separating line
                drawLine(
                    color = ColorBorder,
                    start = Offset(plotLeft, 0f),
                    end = Offset(plotLeft, plotHeight),
                    strokeWidth = 1.5f
                )

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

                val rangeI = max(10.0, (maxI - minI) * 1.2)
                val baseMinI = minI - rangeI * 0.1
                val rangeP = max(20.0, (maxP - minP) * 1.2)
                val baseMinP = minP - rangeP * 0.1
                val rangeV = max(0.5, (maxV - minV) * 1.2)
                val baseMinV = minV - rangeV * 0.1

                // Draw Y-axis scale labels on left margin
                // Primary scale corresponds to first active channel (Current, or Power, or Voltage)
                val (primaryUnit, primaryBase, primaryRange, primaryColor) = when {
                    showCurrent -> Quad("mA", baseMinI, rangeI, ColorCurrent)
                    showPower -> Quad("mW", baseMinP, rangeP, ColorPower)
                    else -> Quad("V", baseMinV, rangeV, ColorVoltage)
                }

                // Draw 5 tick marks on Y-axis
                val tickStyle = TextStyle(
                    color = primaryColor,
                    fontSize = 9.sp,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold
                )

                for (i in 0..hLines) {
                    val fraction = 1.0 - (i.toDouble() / hLines)
                    // Apply zoom and pan transformation to displayed scale
                    val tickVal = primaryBase + (fraction / yZoomScale) * primaryRange - (yPanOffset / plotHeight) * primaryRange
                    val y = plotHeight * i / hLines
                    val textLayout = textMeasurer.measure(
                        text = String.format(Locale.US, "%.1f", tickVal),
                        style = tickStyle
                    )
                    drawText(
                        textLayoutResult = textLayout,
                        topLeft = Offset(plotLeft - textLayout.size.width - 6.dp.toPx(), y - textLayout.size.height / 2f)
                    )
                }

                fun mapX(t: Double): Float {
                    val fraction = (t - startTime) / timeWindowSeconds
                    return (plotLeft + (fraction * plotWidth).toFloat()).coerceIn(plotLeft, totalWidth)
                }

                fun mapY(valInput: Double, baseMin: Double, range: Double): Float {
                    val fraction = (valInput - baseMin) / range
                    val rawY = (plotHeight - (fraction * plotHeight).toFloat())
                    // Apply user's interactive Y zoom and pan
                    val centerY = plotHeight / 2f
                    return ((rawY - centerY) * yZoomScale + centerY + yPanOffset).coerceIn(0f, plotHeight)
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

                // Draw Vertical Dashed Cursor Line & Value Tooltip if user selected a point
                if (selectedPoint != null) {
                    val inspectX = mapX(selectedPoint.timestamp)

                    // 1. Vertical dashed line
                    val dashPath = Path().apply {
                        moveTo(inspectX, 0f)
                        lineTo(inspectX, plotHeight)
                    }
                    drawPath(
                        path = dashPath,
                        color = Color(0xFF2563EB), // Blue 600
                        style = Stroke(
                            width = 1.5.dp.toPx(),
                            pathEffect = PathEffect.dashPathEffect(floatArrayOf(10f, 8f), 0f)
                        )
                    )

                    // 2. Draw point dots on curves
                    if (showCurrent) {
                        val dotY = mapY(selectedPoint.currentMa, baseMinI, rangeI)
                        drawCircle(color = ColorCurrent, radius = 4.dp.toPx(), center = Offset(inspectX, dotY))
                        drawCircle(color = Color.White, radius = 2.dp.toPx(), center = Offset(inspectX, dotY))
                    }
                    if (showPower) {
                        val dotY = mapY(selectedPoint.powerMw, baseMinP, rangeP)
                        drawCircle(color = ColorPower, radius = 4.dp.toPx(), center = Offset(inspectX, dotY))
                        drawCircle(color = Color.White, radius = 2.dp.toPx(), center = Offset(inspectX, dotY))
                    }
                    if (showVoltage) {
                        val dotY = mapY(selectedPoint.voltageV, baseMinV, rangeV)
                        drawCircle(color = ColorVoltage, radius = 4.dp.toPx(), center = Offset(inspectX, dotY))
                        drawCircle(color = Color.White, radius = 2.dp.toPx(), center = Offset(inspectX, dotY))
                    }

                    // 3. Tooltip box displaying instantaneous values
                    val relOffset = selectedPoint.timestamp - latestTime
                    val timeStr = String.format(Locale.US, "时刻: %.1fs", relOffset)
                    val infoCurrent = if (showCurrent) String.format(Locale.US, "电流: %.2fmA", selectedPoint.currentMa) else ""
                    val infoPower = if (showPower) String.format(Locale.US, "功率: %.2fmW", selectedPoint.powerMw) else ""
                    val infoVoltage = if (showVoltage) String.format(Locale.US, "电压: %.3fV", selectedPoint.voltageV) else ""

                    val lines = listOfNotNull(
                        timeStr,
                        infoCurrent.ifEmpty { null },
                        infoPower.ifEmpty { null },
                        infoVoltage.ifEmpty { null }
                    )

                    val tooltipStyle = TextStyle(
                        color = ColorTextMain,
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Medium
                    )

                    var maxLineW = 0f
                    var totalLineH = 0f
                    val layouts = lines.map { l ->
                        val lay = textMeasurer.measure(l, tooltipStyle)
                        maxLineW = max(maxLineW, lay.size.width.toFloat())
                        totalLineH += lay.size.height + 2f
                        lay
                    }

                    val pad = 6.dp.toPx()
                    val boxW = maxLineW + pad * 2
                    val boxH = totalLineH + pad * 2

                    // Determine tooltip X position (flip to left if close to right edge)
                    val boxX = if (inspectX + boxW + 10f > totalWidth) {
                        inspectX - boxW - 8f
                    } else {
                        inspectX + 8f
                    }
                    val boxY = 8.dp.toPx()

                    // Draw Tooltip background with border
                    drawRoundRect(
                        color = ColorBgCard.copy(alpha = 0.95f),
                        topLeft = Offset(boxX, boxY),
                        size = Size(boxW, boxH),
                        cornerRadius = CornerRadius(6.dp.toPx(), 6.dp.toPx())
                    )
                    drawRoundRect(
                        color = Color(0xFF2563EB),
                        topLeft = Offset(boxX, boxY),
                        size = Size(boxW, boxH),
                        cornerRadius = CornerRadius(6.dp.toPx(), 6.dp.toPx()),
                        style = Stroke(width = 1.dp.toPx())
                    )

                    // Draw text inside tooltip
                    var textCurY = boxY + pad
                    for (lay in layouts) {
                        drawText(
                            textLayoutResult = lay,
                            topLeft = Offset(boxX + pad, textCurY)
                        )
                        textCurY += lay.size.height + 2f
                    }
                }
            }

            // Fixed Stationary Time Axis label underneath
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(start = 48.dp, top = 4.dp),
                horizontalArrangement = androidx.compose.foundation.layout.Arrangement.SpaceBetween
            ) {
                Text(
                    text = formatWindowLabel(timeWindowSeconds),
                    fontSize = 10.sp,
                    color = ColorTextMuted
                )
                Text(
                    text = formatWindowLabel(timeWindowSeconds / 2.0),
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

private fun formatWindowLabel(seconds: Double): String {
    return when {
        seconds >= 3600.0 -> String.format(Locale.US, "-%.1fh", seconds / 3600.0)
        seconds >= 60.0 -> String.format(Locale.US, "-%.0fmin", seconds / 60.0)
        else -> String.format(Locale.US, "-%.0fs", seconds)
    }
}

private data class Quad<A, B, C, D>(val first: A, val second: B, val third: C, val fourth: D)

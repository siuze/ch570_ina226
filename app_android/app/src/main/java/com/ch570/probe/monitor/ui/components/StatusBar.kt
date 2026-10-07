package com.ch570.probe.monitor.ui.components

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ch570.probe.monitor.ui.theme.ColorBgCard
import com.ch570.probe.monitor.ui.theme.ColorBorder
import com.ch570.probe.monitor.ui.theme.ColorCurrent
import com.ch570.probe.monitor.ui.theme.ColorTextMain
import com.ch570.probe.monitor.ui.theme.ColorTextMuted
import com.ch570.probe.monitor.viewmodel.UiDashboardState

@Composable
fun StatusBar(
    state: UiDashboardState,
    modifier: Modifier = Modifier
) {
    val infiniteTransition = rememberInfiniteTransition(label = "pulse")
    val alphaAnim by infiniteTransition.animateFloat(
        initialValue = 0.35f,
        targetValue = 1.0f,
        animationSpec = infiniteRepeatable(
            animation = tween(800),
            repeatMode = RepeatMode.Reverse
        ),
        label = "alpha"
    )

    Box(
        modifier = modifier
            .fillMaxWidth()
            .shadow(elevation = 2.dp, shape = RoundedCornerShape(8.dp))
            .background(ColorBgCard, RoundedCornerShape(8.dp))
            .border(1.dp, ColorBorder, RoundedCornerShape(8.dp))
            .padding(horizontal = 14.dp, vertical = 9.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            // Left: breathing dot + label
            Row(
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .size(9.dp)
                        .clip(CircleShape)
                        .background(ColorCurrent.copy(alpha = if (state.isScanning) alphaAnim else 0.4f))
                )
                Spacer(modifier = Modifier.width(7.dp))
                Text(
                    text = if (state.isScanning) "BLE 持续监听中" else "BLE 准备就绪",
                    color = if (state.isScanning) ColorCurrent else ColorTextMuted,
                    fontWeight = FontWeight.Bold,
                    fontSize = 13.sp
                )
            }

            // Right: Signal RSSI & Frame count in single clean line
            Row(
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "信号: ",
                    color = ColorTextMuted,
                    fontSize = 12.sp
                )
                Text(
                    text = "${state.rssi} dBm",
                    color = ColorTextMain,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 12.sp
                )
                Spacer(modifier = Modifier.width(10.dp))
                Text(
                    text = "| 接收: ",
                    color = ColorTextMuted,
                    fontSize = 12.sp
                )
                Text(
                    text = "${state.packetCount} 帧",
                    color = ColorTextMain,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 12.sp
                )
            }
        }
    }
}

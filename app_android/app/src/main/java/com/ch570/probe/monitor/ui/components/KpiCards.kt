package com.ch570.probe.monitor.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ch570.probe.monitor.model.TelemetryFilter
import com.ch570.probe.monitor.ui.theme.ColorBgCard
import com.ch570.probe.monitor.ui.theme.ColorBorder
import com.ch570.probe.monitor.ui.theme.ColorCurrent
import com.ch570.probe.monitor.ui.theme.ColorEnergy
import com.ch570.probe.monitor.ui.theme.ColorPower
import com.ch570.probe.monitor.ui.theme.ColorTextMain
import com.ch570.probe.monitor.ui.theme.ColorTextMuted
import com.ch570.probe.monitor.ui.theme.ColorVoltage
import com.ch570.probe.monitor.viewmodel.UiDashboardState
import java.util.Locale

private val CARD_FIXED_HEIGHT = 114.dp

@Composable
fun KpiCards(
    state: UiDashboardState,
    onResetEnergy: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier.fillMaxWidth()) {
        // Row 1: Current & Power
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            KpiCard(
                title = "采样电流",
                accentColor = ColorCurrent,
                valueText = state.currentDisplay.text,
                unitText = state.currentDisplay.unit,
                minText = "min: " + TelemetryFilter.formatMaxDigits(state.iStats.minVal * state.currentDisplay.rawScale, 4),
                maxText = "max: " + TelemetryFilter.formatMaxDigits(state.iStats.maxVal * state.currentDisplay.rawScale, 4),
                modifier = Modifier
                    .weight(1f)
                    .height(CARD_FIXED_HEIGHT)
            )

            KpiCard(
                title = "实时功率",
                accentColor = ColorPower,
                valueText = state.powerDisplay.text,
                unitText = state.powerDisplay.unit,
                minText = "min: " + TelemetryFilter.formatMaxDigits(state.pStats.minVal * state.powerDisplay.rawScale, 4),
                maxText = "max: " + TelemetryFilter.formatMaxDigits(state.pStats.maxVal * state.powerDisplay.rawScale, 4),
                modifier = Modifier
                    .weight(1f)
                    .height(CARD_FIXED_HEIGHT)
            )
        }

        Spacer(modifier = Modifier.height(10.dp))

        // Row 2: Voltage & Energy Accumulation
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            KpiCard(
                title = "母线电压",
                accentColor = ColorVoltage,
                valueText = state.voltageDisplay.text,
                unitText = state.voltageDisplay.unit,
                minText = "min: " + TelemetryFilter.formatMaxDigits(state.vStats.minVal * state.voltageDisplay.rawScale, 4),
                maxText = "max: " + TelemetryFilter.formatMaxDigits(state.vStats.maxVal * state.voltageDisplay.rawScale, 4),
                modifier = Modifier
                    .weight(1f)
                    .height(CARD_FIXED_HEIGHT)
            )

            EnergyCard(
                mah = state.accumulatedMah,
                mwh = state.accumulatedMwh,
                onReset = onResetEnergy,
                modifier = Modifier
                    .weight(1f)
                    .height(CARD_FIXED_HEIGHT)
            )
        }
    }
}

@Composable
fun KpiCard(
    title: String,
    accentColor: Color,
    valueText: String,
    unitText: String,
    minText: String,
    maxText: String,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .shadow(3.dp, RoundedCornerShape(8.dp))
            .background(ColorBgCard, RoundedCornerShape(8.dp))
            .border(1.dp, ColorBorder, RoundedCornerShape(8.dp))
            .padding(horizontal = 12.dp, vertical = 9.dp)
    ) {
        Column(
            modifier = Modifier.fillMaxHeight(),
            verticalArrangement = Arrangement.SpaceBetween
        ) {
            // 1. Header: Chinese title only (never wraps) + Accent bar
            Column {
                Text(
                    text = title,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Bold,
                    color = accentColor,
                    maxLines = 1
                )
                Spacer(modifier = Modifier.height(3.dp))
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(2.dp)
                        .background(accentColor.copy(alpha = 0.5f))
                )
            }

            // 2. Middle: Large Monospace value + fixed unit
            Row(
                verticalAlignment = Alignment.Bottom,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(
                    text = valueText,
                    fontSize = 24.sp,
                    fontWeight = FontWeight.ExtraBold,
                    fontFamily = FontFamily.Monospace,
                    color = ColorTextMain,
                    maxLines = 1
                )
                Spacer(modifier = Modifier.width(6.dp))
                Text(
                    text = unitText,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Bold,
                    color = accentColor,
                    modifier = Modifier.padding(bottom = 2.dp),
                    maxLines = 1
                )
            }

            // 3. Bottom: Separated min & max row (never conflicts with unit)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = minText,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                    color = ColorTextMuted,
                    maxLines = 1
                )
                Text(
                    text = maxText,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                    color = ColorTextMuted,
                    maxLines = 1
                )
            }
        }
    }
}

@Composable
fun EnergyCard(
    mah: Double,
    mwh: Double,
    onReset: () -> Unit,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .shadow(3.dp, RoundedCornerShape(8.dp))
            .background(ColorBgCard, RoundedCornerShape(8.dp))
            .border(1.dp, ColorBorder, RoundedCornerShape(8.dp))
            .padding(horizontal = 12.dp, vertical = 9.dp)
    ) {
        Column(
            modifier = Modifier.fillMaxHeight(),
            verticalArrangement = Arrangement.SpaceBetween
        ) {
            // Header: Title on left, Reset button on right
            Column {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "电量累计",
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Bold,
                        color = ColorEnergy,
                        maxLines = 1
                    )
                    Button(
                        onClick = onReset,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = Color(0xFFEEF2FF),
                            contentColor = ColorEnergy
                        ),
                        shape = RoundedCornerShape(4.dp),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                        modifier = Modifier.height(20.dp)
                    ) {
                        Text(text = "清零", fontSize = 10.sp, fontWeight = FontWeight.Bold)
                    }
                }
                Spacer(modifier = Modifier.height(3.dp))
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(2.dp)
                        .background(ColorEnergy.copy(alpha = 0.5f))
                )
            }

            // Two distinct lines: Capacity (mAh) above, Energy (mWh) below
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                // Line 1: mAh
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.Bottom,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "容量:",
                        fontSize = 11.sp,
                        color = ColorTextMuted
                    )
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(
                            text = String.format(Locale.US, "%.3f", mah),
                            fontSize = 16.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace,
                            color = ColorTextMain
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = "mAh",
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Bold,
                            color = ColorEnergy
                        )
                    }
                }

                // Line 2: mWh
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.Bottom,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "能量:",
                        fontSize = 11.sp,
                        color = ColorTextMuted
                    )
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(
                            text = String.format(Locale.US, "%.3f", mwh),
                            fontSize = 16.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace,
                            color = ColorTextMain
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = "mWh",
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Bold,
                            color = ColorEnergy
                        )
                    }
                }
            }
        }
    }
}

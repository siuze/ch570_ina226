package com.ch570.probe.monitor.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
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
                zhTitle = "采样电流",
                enTitle = "SHUNT CURRENT",
                accentColor = ColorCurrent,
                valueText = state.currentDisplay.text,
                unitText = state.currentDisplay.unit,
                minText = "min: " + TelemetryFilter.formatMaxDigits(state.iStats.minVal * state.currentDisplay.rawScale, 4),
                maxText = "max: " + TelemetryFilter.formatMaxDigits(state.iStats.maxVal * state.currentDisplay.rawScale, 4),
                modifier = Modifier.weight(1f)
            )

            KpiCard(
                zhTitle = "实时功率",
                enTitle = "INSTANT POWER",
                accentColor = ColorPower,
                valueText = state.powerDisplay.text,
                unitText = state.powerDisplay.unit,
                minText = "min: " + TelemetryFilter.formatMaxDigits(state.pStats.minVal * state.powerDisplay.rawScale, 4),
                maxText = "max: " + TelemetryFilter.formatMaxDigits(state.pStats.maxVal * state.powerDisplay.rawScale, 4),
                modifier = Modifier.weight(1f)
            )
        }

        Spacer(modifier = Modifier.height(10.dp))

        // Row 2: Voltage & Energy Accumulation
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            KpiCard(
                zhTitle = "母线电压",
                enTitle = "BUS VOLTAGE",
                accentColor = ColorVoltage,
                valueText = state.voltageDisplay.text,
                unitText = state.voltageDisplay.unit,
                minText = "min: " + TelemetryFilter.formatMaxDigits(state.vStats.minVal * state.voltageDisplay.rawScale, 4),
                maxText = "max: " + TelemetryFilter.formatMaxDigits(state.vStats.maxVal * state.voltageDisplay.rawScale, 4),
                modifier = Modifier.weight(1f)
            )

            EnergyCard(
                mah = state.accumulatedMah,
                mwh = state.accumulatedMwh,
                onReset = onResetEnergy,
                modifier = Modifier.weight(1f)
            )
        }
    }
}

@Composable
fun KpiCard(
    zhTitle: String,
    enTitle: String,
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
            .padding(10.dp)
    ) {
        Column {
            // Header: zhTitle + enTitle + Accent line
            Row(
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = zhTitle,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Bold,
                    color = accentColor
                )
                Spacer(modifier = Modifier.width(4.dp))
                Text(
                    text = enTitle,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Medium,
                    color = accentColor.copy(alpha = 0.8f)
                )
            }

            Spacer(modifier = Modifier.height(4.dp))
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(2.dp)
                    .background(accentColor.copy(alpha = 0.4f))
            )
            Spacer(modifier = Modifier.height(6.dp))

            // Body: Large value + unit on left, min/max stats on right
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Bottom,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(
                    verticalAlignment = Alignment.Bottom
                ) {
                    Text(
                        text = valueText,
                        fontSize = 24.sp,
                        fontWeight = FontWeight.ExtraBold,
                        fontFamily = FontFamily.Monospace,
                        color = ColorTextMain
                    )
                    Spacer(modifier = Modifier.width(4.dp))
                    Text(
                        text = unitText,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Bold,
                        color = accentColor,
                        modifier = Modifier.padding(bottom = 2.dp)
                    )
                }

                Column(
                    horizontalAlignment = Alignment.End
                ) {
                    Text(
                        text = minText,
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        color = ColorTextMuted
                    )
                    Text(
                        text = maxText,
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        color = ColorTextMuted
                    )
                }
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
            .padding(10.dp)
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "电量累计",
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Bold,
                        color = ColorEnergy
                    )
                    Spacer(modifier = Modifier.width(4.dp))
                    Text(
                        text = "CAPACITY & ENERGY",
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Medium,
                        color = ColorEnergy.copy(alpha = 0.8f)
                    )
                }

                Button(
                    onClick = onReset,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFFEEF2FF),
                        contentColor = ColorEnergy
                    ),
                    shape = RoundedCornerShape(4.dp),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                    modifier = Modifier.height(22.dp)
                ) {
                    Text(text = "清零", fontSize = 10.sp, fontWeight = FontWeight.SemiBold)
                }
            }

            Spacer(modifier = Modifier.height(4.dp))
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(2.dp)
                    .background(ColorEnergy.copy(alpha = 0.4f))
            )
            Spacer(modifier = Modifier.height(6.dp))

            // Two-line layout: mAh and mWh
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        text = String.format(Locale.US, "%.3f", mah),
                        fontSize = 17.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        color = ColorTextMain
                    )
                    Spacer(modifier = Modifier.width(3.dp))
                    Text(
                        text = "mAh",
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Bold,
                        color = ColorEnergy
                    )
                }

                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        text = String.format(Locale.US, "%.3f", mwh),
                        fontSize = 17.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        color = ColorTextMain
                    )
                    Spacer(modifier = Modifier.width(3.dp))
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

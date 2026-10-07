package com.ch570.probe.monitor.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CheckboxDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ch570.probe.monitor.ui.theme.ColorBgCard
import com.ch570.probe.monitor.ui.theme.ColorBorder
import com.ch570.probe.monitor.ui.theme.ColorCurrent
import com.ch570.probe.monitor.ui.theme.ColorDanger
import com.ch570.probe.monitor.ui.theme.ColorPower
import com.ch570.probe.monitor.ui.theme.ColorTextMain
import com.ch570.probe.monitor.ui.theme.ColorTextMuted
import com.ch570.probe.monitor.ui.theme.ColorVoltage
import com.ch570.probe.monitor.viewmodel.UiDashboardState

@Composable
fun Controls(
    state: UiDashboardState,
    onTogglePause: () -> Unit,
    onClearData: () -> Unit,
    onSetTimeWindow: (Double) -> Unit,
    onToggleCurrent: (Boolean) -> Unit,
    onTogglePower: (Boolean) -> Unit,
    onToggleVoltage: (Boolean) -> Unit,
    onExportCsv: () -> Unit,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .shadow(2.dp, RoundedCornerShape(8.dp))
            .background(ColorBgCard, RoundedCornerShape(8.dp))
            .border(1.dp, ColorBorder, RoundedCornerShape(8.dp))
            .padding(10.dp)
    ) {
        Column {
            // Row 1: Time Window selector + Channel toggles
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                // Time Window chips
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "窗口:",
                        fontSize = 12.sp,
                        color = ColorTextMuted
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    listOf(10.0 to "10s", 30.0 to "30s", 60.0 to "60s").forEach { (sec, label) ->
                        val isSelected = state.timeWindowSeconds == sec
                        Box(
                            modifier = Modifier
                                .clickable { onSetTimeWindow(sec) }
                                .background(
                                    if (isSelected) ColorVoltage else Color.Transparent,
                                    RoundedCornerShape(4.dp)
                                )
                                .border(
                                    1.dp,
                                    if (isSelected) ColorVoltage else ColorBorder,
                                    RoundedCornerShape(4.dp)
                                )
                                .padding(horizontal = 8.dp, vertical = 3.dp)
                        ) {
                            Text(
                                text = label,
                                fontSize = 11.sp,
                                fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                                color = if (isSelected) Color.White else ColorTextMain
                            )
                        }
                        Spacer(modifier = Modifier.width(4.dp))
                    }
                }

                // Channel checkboxes
                Row(verticalAlignment = Alignment.CenterVertically) {
                    ChannelCheckbox(
                        label = "电流",
                        checked = state.showCurrent,
                        color = ColorCurrent,
                        onCheckedChange = onToggleCurrent
                    )
                    ChannelCheckbox(
                        label = "功率",
                        checked = state.showPower,
                        color = ColorPower,
                        onCheckedChange = onTogglePower
                    )
                    ChannelCheckbox(
                        label = "电压",
                        checked = state.showVoltage,
                        color = ColorVoltage,
                        onCheckedChange = onToggleVoltage
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Row 2: Action buttons (Pause, Clear, Export CSV)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    onClick = onTogglePause,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (state.isPaused) Color(0xFFFEF3C7) else Color(0xFFF1F5F9),
                        contentColor = if (state.isPaused) ColorPower else ColorTextMain
                    ),
                    shape = RoundedCornerShape(4.dp),
                    modifier = Modifier.weight(1f).height(36.dp)
                ) {
                    Text(
                        text = if (state.isPaused) "继续采集" else "暂停采集",
                        fontSize = 12.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                }

                Button(
                    onClick = onClearData,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFFFEE2E2),
                        contentColor = ColorDanger
                    ),
                    shape = RoundedCornerShape(4.dp),
                    modifier = Modifier.weight(1f).height(36.dp)
                ) {
                    Text(text = "清屏数据", fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                }

                OutlinedButton(
                    onClick = onExportCsv,
                    shape = RoundedCornerShape(4.dp),
                    modifier = Modifier.weight(1.2f).height(36.dp)
                ) {
                    Text(text = "导出 CSV", fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = ColorVoltage)
                }
            }
        }
    }
}

@Composable
fun ChannelCheckbox(
    label: String,
    checked: Boolean,
    color: Color,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .clickable { onCheckedChange(!checked) }
            .padding(horizontal = 3.dp)
    ) {
        Checkbox(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = CheckboxDefaults.colors(
                checkedColor = color,
                uncheckedColor = ColorBorder
            ),
            modifier = Modifier.size(24.dp)
        )
        Text(
            text = label,
            fontSize = 11.sp,
            fontWeight = FontWeight.Medium,
            color = if (checked) color else ColorTextMuted
        )
    }
}

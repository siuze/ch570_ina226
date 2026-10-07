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
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
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
import com.ch570.probe.monitor.viewmodel.TIME_WINDOW_OPTIONS
import com.ch570.probe.monitor.viewmodel.TimeWindowOption
import com.ch570.probe.monitor.viewmodel.UiDashboardState

@Composable
fun Controls(
    state: UiDashboardState,
    onTogglePause: () -> Unit,
    onClearData: () -> Unit,
    onSetTimeWindow: (TimeWindowOption) -> Unit,
    onToggleCurrent: (Boolean) -> Unit,
    onTogglePower: (Boolean) -> Unit,
    onToggleVoltage: (Boolean) -> Unit,
    modifier: Modifier = Modifier
) {
    var expanded by remember { mutableStateOf(false) }

    Box(
        modifier = modifier
            .fillMaxWidth()
            .shadow(2.dp, RoundedCornerShape(8.dp))
            .background(ColorBgCard, RoundedCornerShape(8.dp))
            .border(1.dp, ColorBorder, RoundedCornerShape(8.dp))
            .padding(10.dp)
    ) {
        Column {
            // Row 1: Time Window dropdown + Channel toggles
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                // Dropdown selector for Time Window
                Box {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier
                            .background(Color(0xFFF1F5F9), RoundedCornerShape(6.dp))
                            .border(1.dp, ColorBorder, RoundedCornerShape(6.dp))
                            .clickable { expanded = true }
                            .padding(horizontal = 10.dp, vertical = 6.dp)
                    ) {
                        Text(
                            text = "窗口: ",
                            fontSize = 12.sp,
                            color = ColorTextMuted
                        )
                        Text(
                            text = state.selectedWindowLabel,
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Bold,
                            color = ColorVoltage
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = "▼",
                            fontSize = 9.sp,
                            color = ColorTextMuted
                        )
                    }

                    DropdownMenu(
                        expanded = expanded,
                        onDismissRequest = { expanded = false },
                        modifier = Modifier.background(ColorBgCard)
                    ) {
                        TIME_WINDOW_OPTIONS.forEach { opt ->
                            val isSelected = opt.label == state.selectedWindowLabel
                            DropdownMenuItem(
                                text = {
                                    Text(
                                        text = opt.label,
                                        fontSize = 13.sp,
                                        fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                                        color = if (isSelected) ColorVoltage else ColorTextMain
                                    )
                                },
                                onClick = {
                                    onSetTimeWindow(opt)
                                    expanded = false
                                }
                            )
                        }
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

            // Row 2: Action buttons (Pause/Resume, Clear Data) - NO CSV EXPORT
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Button(
                    onClick = onTogglePause,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (state.isPaused) Color(0xFFFEF3C7) else Color(0xFFF1F5F9),
                        contentColor = if (state.isPaused) ColorPower else ColorTextMain
                    ),
                    shape = RoundedCornerShape(6.dp),
                    modifier = Modifier.weight(1f).height(38.dp)
                ) {
                    Text(
                        text = if (state.isPaused) "继续采集" else "暂停采集",
                        fontSize = 13.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                }

                Button(
                    onClick = onClearData,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFFFEE2E2),
                        contentColor = ColorDanger
                    ),
                    shape = RoundedCornerShape(6.dp),
                    modifier = Modifier.weight(1f).height(38.dp)
                ) {
                    Text(
                        text = "清屏数据",
                        fontSize = 13.sp,
                        fontWeight = FontWeight.SemiBold
                    )
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
            .padding(horizontal = 2.dp)
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
            fontSize = 12.sp,
            fontWeight = FontWeight.Medium,
            color = if (checked) color else ColorTextMuted
        )
    }
}

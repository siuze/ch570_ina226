package com.ch570.probe.monitor.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable

private val LightColorScheme = lightColorScheme(
    primary = ColorVoltage,
    onPrimary = ColorBgCard,
    background = ColorBgWindow,
    onBackground = ColorTextMain,
    surface = ColorBgCard,
    onSurface = ColorTextMain,
    outline = ColorBorder
)

@Composable
fun CH570ProbeMonitorTheme(
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = LightColorScheme,
        content = content
    )
}

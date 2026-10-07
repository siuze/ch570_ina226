package com.ch570.probe.monitor.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ch570.probe.monitor.ui.components.Controls
import com.ch570.probe.monitor.ui.components.KpiCards
import com.ch570.probe.monitor.ui.components.StatusBar
import com.ch570.probe.monitor.ui.components.WaveformView
import com.ch570.probe.monitor.ui.theme.ColorBgWindow
import com.ch570.probe.monitor.ui.theme.ColorTextMuted
import com.ch570.probe.monitor.viewmodel.MainViewModel

@Composable
fun MainScreen(
    viewModel: MainViewModel,
    modifier: Modifier = Modifier
) {
    val state by viewModel.uiState.collectAsState()
    val snackbarHostState = remember { SnackbarHostState() }

    LaunchedEffect(state.lastError) {
        state.lastError?.let {
            snackbarHostState.showSnackbar(it)
        }
    }

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(ColorBgWindow)
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(12.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            // 1. Status Bar (Continuous listening, signal RSSI & frame count, no stop button)
            StatusBar(state = state)

            // 2. 4 Core KPI Cards (Fixed height, monospace numbers, fixed unit, energy split rows)
            KpiCards(
                state = state,
                onResetEnergy = { viewModel.resetEnergy() }
            )

            // 3. Real-time Waveform View (Y-axis tick ruler, pan/zoom gestures, tap inspection, double tap dismiss)
            WaveformView(
                points = state.chartPoints,
                timeWindowSeconds = state.timeWindowSeconds,
                showCurrent = state.showCurrent,
                showPower = state.showPower,
                showVoltage = state.showVoltage,
                selectedPoint = state.selectedPoint,
                onSelectPoint = { viewModel.selectPointAtTime(it) },
                onClearSelection = { viewModel.clearSelectedPoint() }
            )

            // 4. Waveform Controls (Dropdown window selection, channel toggles, pause/clear, no CSV export)
            Controls(
                state = state,
                onTogglePause = { viewModel.togglePause() },
                onClearData = { viewModel.clearData() },
                onSetTimeWindow = { viewModel.setTimeWindow(it) },
                onToggleCurrent = { viewModel.toggleShowCurrent(it) },
                onTogglePower = { viewModel.toggleShowPower(it) },
                onToggleVoltage = { viewModel.toggleShowVoltage(it) }
            )

            Spacer(modifier = Modifier.height(6.dp))

            // Footer note
            Box(
                modifier = Modifier.fillMaxWidth(),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = "Probe BLE 0xFCD2 | Android 上位机 v2.0",
                    fontSize = 11.sp,
                    color = ColorTextMuted,
                    fontWeight = FontWeight.Medium
                )
            }
        }

        SnackbarHost(
            hostState = snackbarHostState,
            modifier = Modifier.align(Alignment.BottomCenter)
        )
    }
}

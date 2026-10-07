package com.ch570.probe.monitor.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.ch570.probe.monitor.ble.BleScanner
import com.ch570.probe.monitor.model.FormattedMetric
import com.ch570.probe.monitor.model.MetricStats
import com.ch570.probe.monitor.model.TelemetryFilter
import com.ch570.probe.monitor.model.TelemetryHistory
import com.ch570.probe.monitor.model.TelemetryPoint
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

data class UiDashboardState(
    val isScanning: Boolean = false,
    val isPaused: Boolean = false,
    val deviceMac: String = "CA:57:09:1E:A2:26",
    val rssi: Int = 0,
    val packetCount: Long = 0L,
    val lastError: String? = null,

    // KPI Cards formatted data
    val currentDisplay: FormattedMetric = FormattedMetric("0.00", "mA", false, 1.0),
    val powerDisplay: FormattedMetric = FormattedMetric("0.00", "mW", false, 1.0),
    val voltageDisplay: FormattedMetric = FormattedMetric("0.0", "mV", false, 1000.0),
    val accumulatedMah: Double = 0.0,
    val accumulatedMwh: Double = 0.0,

    // Rolling statistics (matching card metrics)
    val iStats: MetricStats = MetricStats(),
    val pStats: MetricStats = MetricStats(),
    val vStats: MetricStats = MetricStats(),

    // Chart display configuration
    val timeWindowSeconds: Double = 30.0,
    val showCurrent: Boolean = true,
    val showPower: Boolean = true,
    val showVoltage: Boolean = false,
    val chartPoints: List<TelemetryPoint> = emptyList()
)

class MainViewModel(application: Application) : AndroidViewModel(application) {

    val bleScanner = BleScanner(application.applicationContext)

    private val history = TelemetryHistory(15000)
    private val filter = TelemetryFilter()

    private val startNanoTime = System.nanoTime()

    private val _uiState = MutableStateFlow(UiDashboardState())
    val uiState: StateFlow<UiDashboardState> = _uiState.asStateFlow()

    private var packetCount = 0L
    private var lastRssi = 0
    private var deviceMac = "--:--:--:--:--:--"

    private val vStats = MetricStats()
    private val iStats = MetricStats()
    private val pStats = MetricStats()

    init {
        // Collect BLE scan events
        viewModelScope.launch(Dispatchers.Default) {
            bleScanner.scanEvents.collect { event ->
                val nowSec = (System.nanoTime() - startNanoTime) / 1_000_000_000.0
                val epochMs = System.currentTimeMillis()

                packetCount++
                lastRssi = event.rssi
                deviceMac = event.macAddress

                val pt = TelemetryPoint(
                    timestamp = nowSec,
                    epochMs = epochMs,
                    voltageV = event.parsedData.voltageV,
                    currentMa = event.parsedData.currentMa,
                    powerMw = event.parsedData.powerMw,
                    rssi = event.rssi,
                    sequence = event.parsedData.sequence
                )

                if (!_uiState.value.isPaused) {
                    history.push(pt)
                }

                // Update filter on new sample
                filter.processRecentSamples(history.getRecent(3))
                refreshUiState()
            }
        }

        // Monitor scanner status and error
        viewModelScope.launch {
            bleScanner.isScanning.collect { scanning ->
                _uiState.value = _uiState.value.copy(isScanning = scanning)
            }
        }
        viewModelScope.launch {
            bleScanner.lastError.collect { error ->
                _uiState.value = _uiState.value.copy(lastError = error)
            }
        }
    }

    private fun refreshUiState() {
        val windowSec = _uiState.value.timeWindowSeconds
        history.getWindowStats(windowSec, vStats, iStats, pStats)
        val chartPts = history.getWindowPoints(windowSec)

        _uiState.value = _uiState.value.copy(
            deviceMac = deviceMac,
            rssi = lastRssi,
            packetCount = packetCount,
            currentDisplay = filter.getCurrentDisplay(),
            powerDisplay = filter.getPowerDisplay(),
            voltageDisplay = filter.getVoltageDisplay(),
            accumulatedMah = history.getAccumulatedMah(),
            accumulatedMwh = history.getAccumulatedMwh(),
            iStats = iStats.copy(),
            pStats = pStats.copy(),
            vStats = vStats.copy(),
            chartPoints = chartPts
        )
    }

    fun startScan() {
        bleScanner.startScan()
    }

    fun stopScan() {
        bleScanner.stopScan()
    }

    fun togglePause() {
        val nextPaused = !_uiState.value.isPaused
        _uiState.value = _uiState.value.copy(isPaused = nextPaused)
    }

    fun clearData() {
        history.clear()
        filter.reset()
        packetCount = 0L
        refreshUiState()
    }

    fun resetEnergy() {
        history.resetEnergy()
        refreshUiState()
    }

    fun setTimeWindow(seconds: Double) {
        _uiState.value = _uiState.value.copy(timeWindowSeconds = seconds)
        refreshUiState()
    }

    fun toggleShowCurrent(show: Boolean) {
        _uiState.value = _uiState.value.copy(showCurrent = show)
    }

    fun toggleShowPower(show: Boolean) {
        _uiState.value = _uiState.value.copy(showPower = show)
    }

    fun toggleShowVoltage(show: Boolean) {
        _uiState.value = _uiState.value.copy(showVoltage = show)
    }

    /**
     * Export all collected points to standard CSV format identical to win_app.
     * timestamp_ms,time,current_mA,voltage_V,power_mW,capacity_mAh,energy_mWh
     */
    fun exportCsvData(): String {
        val allPoints = history.getAllPoints()
        val sb = StringBuilder()
        sb.append("timestamp_ms,time,current_mA,voltage_V,power_mW,capacity_mAh,energy_mWh\n")

        val sdf = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)
        var accAh = 0.0
        var accWh = 0.0
        var lastTs = -1.0

        for (p in allPoints) {
            if (lastTs >= 0.0) {
                val dt = p.timestamp - lastTs
                if (dt in 0.0..10.0) {
                    val hours = dt / 3600.0
                    accAh += (p.currentMa / 1000.0) * hours
                    accWh += (p.powerMw / 1000.0) * hours
                }
            }
            lastTs = p.timestamp

            val timeStr = sdf.format(Date(p.epochMs))
            sb.append(p.epochMs).append(",")
                .append(timeStr).append(",")
                .append(String.format(Locale.US, "%.3f", p.currentMa)).append(",")
                .append(String.format(Locale.US, "%.6f", p.voltageV)).append(",")
                .append(String.format(Locale.US, "%.3f", p.powerMw)).append(",")
                .append(String.format(Locale.US, "%.6f", accAh * 1000.0)).append(",")
                .append(String.format(Locale.US, "%.6f", accWh * 1000.0)).append("\n")
        }
        return sb.toString()
    }
}

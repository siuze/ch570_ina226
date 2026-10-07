package com.ch570.probe.monitor.model

import java.util.Locale
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min

/**
 * Filter and format logic for jitter-free KPI card display,
 * strictly matching docs/上位机数据显示与抗抖设计.md and app_win.
 */
class TelemetryFilter {

    private var smoothInitialized = false
    private var filterLastTimestamp = -1.0

    var smoothCurrent: Double = 0.0
        private set
    var smoothPower: Double = 0.0
        private set
    var smoothVoltage: Double = 0.0
        private set

    // Unit hysteresis states
    private var currentInA: Boolean = false
    private var powerInW: Boolean = false
    private var voltageInV: Boolean = false

    /**
     * Feed latest up to 3 points to filter step.
     */
    fun processRecentSamples(recentPoints: List<TelemetryPoint>) {
        if (recentPoints.isEmpty()) return
        val newest = recentPoints.last()
        val newestTs = newest.timestamp

        if (newestTs == filterLastTimestamp) return

        val medianCurrent = median(recentPoints.map { it.currentMa })
        val medianPower = median(recentPoints.map { it.powerMw })
        val medianVoltage = median(recentPoints.map { it.voltageV })

        if (!smoothInitialized || newestTs < filterLastTimestamp) {
            smoothCurrent = medianCurrent
            smoothPower = medianPower
            smoothVoltage = medianVoltage
            smoothInitialized = true
        } else {
            val dt = (newestTs - filterLastTimestamp).coerceIn(0.01, 2.0)
            val alpha = 1.0 - exp(-dt / 0.8)
            smoothCurrent += (medianCurrent - smoothCurrent) * alpha
            smoothPower += (medianPower - smoothPower) * alpha
            smoothVoltage += (medianVoltage - smoothVoltage) * alpha
        }
        filterLastTimestamp = newestTs

        // Unit hysteresis updates
        val curAbs = abs(smoothCurrent)
        val pwrAbs = abs(smoothPower)
        val voltAbs = abs(smoothVoltage)

        currentInA = if (currentInA) curAbs >= 1350.0 else curAbs >= 1500.0
        powerInW = if (powerInW) pwrAbs >= 850.0 else pwrAbs >= 1000.0
        voltageInV = if (voltageInV) voltAbs >= 1.8 else voltAbs >= 2.0
    }

    fun getCurrentDisplay(): FormattedMetric {
        val scale = if (currentInA) 0.001 else 1.0
        val unit = if (currentInA) "A" else "mA"
        val value = smoothCurrent * scale
        val text = if (currentInA) {
            String.format(Locale.US, "%.3f", value)
        } else {
            format5Slot(value, 3)
        }
        return FormattedMetric(text = text, unit = unit, isLargeUnit = currentInA, rawScale = scale)
    }

    fun getPowerDisplay(): FormattedMetric {
        val scale = if (powerInW) 0.001 else 1.0
        val unit = if (powerInW) "W" else "mW"
        val value = smoothPower * scale
        val text = if (powerInW) {
            String.format(Locale.US, "%.3f", value)
        } else {
            format5Slot(value, 2)
        }
        return FormattedMetric(text = text, unit = unit, isLargeUnit = powerInW, rawScale = scale)
    }

    fun getVoltageDisplay(): FormattedMetric {
        val scale = if (voltageInV) 1.0 else 1000.0
        val unit = if (voltageInV) "V" else "mV"
        val value = smoothVoltage * scale
        val text = if (voltageInV) {
            String.format(Locale.US, "%.3f", value)
        } else {
            format5Slot(value, 1)
        }
        return FormattedMetric(text = text, unit = unit, isLargeUnit = voltageInV, rawScale = scale)
    }

    fun reset() {
        smoothInitialized = false
        filterLastTimestamp = -1.0
        smoothCurrent = 0.0
        smoothPower = 0.0
        smoothVoltage = 0.0
        currentInA = false
        powerInW = false
        voltageInV = false
    }

    companion object {
        fun median(values: List<Double>): Double {
            if (values.isEmpty()) return 0.0
            val sorted = values.sorted()
            return sorted[sorted.size / 2]
        }

        fun format5Slot(valInput: Double, decimalsUnder100: Int = 2): String {
            if (valInput.isNaN() || valInput.isInfinite()) return "0.00"
            val absVal = abs(valInput)
            val prefix = if (valInput < -0.0001) "-" else ""
            val formatted = when {
                absVal >= 10000.0 -> String.format(Locale.US, "%.0f", absVal)
                absVal >= 1000.0 -> String.format(Locale.US, "%.1f", absVal)
                absVal >= 100.0 -> String.format(Locale.US, "%.2f", absVal)
                absVal < 100.0 && decimalsUnder100 >= 3 -> String.format(Locale.US, "%.3f", absVal)
                else -> String.format(Locale.US, "%.2f", absVal)
            }
            return prefix + formatted
        }

        fun formatMaxDigits(value: Double, maxDigits: Int = 4): String {
            if (value.isNaN() || value.isInfinite()) return "0"
            val absVal = abs(value)
            if (absVal == 0.0) return "0.0"
            val intDigits = if (absVal < 1.0) 1 else kotlin.math.floor(kotlin.math.log10(absVal)).toInt() + 1
            val decimals = max(0, maxDigits - intDigits)
            return String.format(Locale.US, "%.${decimals}f", value)
        }
    }
}

data class FormattedMetric(
    val text: String,
    val unit: String,
    val isLargeUnit: Boolean,
    val rawScale: Double
)

package com.ch570.probe.monitor.model

/**
 * Statistics container for min, max, avg and current values over a time window.
 */
data class MetricStats(
    var current: Double = 0.0,
    var minVal: Double = 0.0,
    var maxVal: Double = 0.0,
    var avgVal: Double = 0.0,
    var sum: Double = 0.0,
    var count: Long = 0L
) {
    fun update(value: Double) {
        current = value
        updateStatsOnly(value)
    }

    fun updateStatsOnly(value: Double) {
        if (count == 0L) {
            minVal = value
            maxVal = value
            sum = value
            count = 1L
        } else {
            if (value < minVal) minVal = value
            if (value > maxVal) maxVal = value
            sum += value
            count++
        }
        avgVal = if (count > 0L) sum / count else 0.0
    }

    fun reset() {
        current = 0.0
        minVal = 0.0
        maxVal = 0.0
        avgVal = 0.0
        sum = 0.0
        count = 0L
    }
}

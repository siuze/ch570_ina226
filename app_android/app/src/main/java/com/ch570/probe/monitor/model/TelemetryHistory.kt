package com.ch570.probe.monitor.model

import java.util.ArrayList
import kotlin.math.abs

/**
 * High-performance telemetry buffer keeping history samples, rolling window statistics,
 * and capacity/energy integration matching app_win TelemetryHistory.
 * Expanded to 100,000 points to easily support long windows (up to 5 hours).
 */
class TelemetryHistory(private val capacity: Int = 100000) {

    private val lock = Any()
    private val points = ArrayList<TelemetryPoint>(capacity)

    // Window / All-time stats
    private val vStats = MetricStats()
    private val iStats = MetricStats()
    private val pStats = MetricStats()

    // Energy integration (internal unit: Ah and Wh)
    private var accumulatedAh: Double = 0.0
    private var accumulatedWh: Double = 0.0
    private var lastEnergyTimestamp: Double = -1.0

    fun push(pt: TelemetryPoint) {
        synchronized(lock) {
            if (points.size >= capacity) {
                points.removeAt(0)
            }
            points.add(pt)

            vStats.update(pt.voltageV)
            iStats.update(pt.currentMa)
            pStats.update(pt.powerMw)

            // Integrate capacity and energy
            if (lastEnergyTimestamp >= 0.0) {
                val dt = pt.timestamp - lastEnergyTimestamp
                if (dt > 0.0 && dt < 10.0) { // reject anomalous gap (e.g. reconnection)
                    val hours = dt / 3600.0
                    accumulatedAh += (pt.currentMa / 1000.0) * hours
                    accumulatedWh += (pt.powerMw / 1000.0) * hours
                }
            }
            lastEnergyTimestamp = pt.timestamp
        }
    }

    fun clear() {
        synchronized(lock) {
            points.clear()
            vStats.reset()
            iStats.reset()
            pStats.reset()
        }
    }

    fun resetEnergy() {
        synchronized(lock) {
            accumulatedAh = 0.0
            accumulatedWh = 0.0
            lastEnergyTimestamp = -1.0
        }
    }

    fun getAccumulatedMah(): Double = synchronized(lock) { accumulatedAh * 1000.0 }
    fun getAccumulatedMwh(): Double = synchronized(lock) { accumulatedWh * 1000.0 }

    /**
     * Get recent points within [windowSeconds] for waveform rendering.
     * Downsamples if point count exceeds 1500 for maximum rendering performance.
     */
    fun getWindowPoints(windowSeconds: Double): List<TelemetryPoint> {
        synchronized(lock) {
            if (points.isEmpty()) return emptyList()
            val lastTs = points.last().timestamp
            val cutoff = lastTs - windowSeconds
            val inWindow = ArrayList<TelemetryPoint>()
            for (i in points.indices.reversed()) {
                val p = points[i]
                if (p.timestamp < cutoff) break
                inWindow.add(p)
            }
            inWindow.reverse()

            if (inWindow.size <= 1500) {
                return inWindow
            }

            // Downsample for smooth Canvas rendering when viewing hours of data
            val step = inWindow.size.toDouble() / 1500.0
            val sampled = ArrayList<TelemetryPoint>(1500)
            var curIndex = 0.0
            while (curIndex < inWindow.size) {
                sampled.add(inWindow[curIndex.toInt()])
                curIndex += step
            }
            if (sampled.lastOrNull() != inWindow.lastOrNull()) {
                sampled.add(inWindow.last())
            }
            return sampled
        }
    }

    /**
     * Find nearest telemetry point to a target relative timestamp.
     */
    fun findNearestPoint(targetTime: Double, windowSeconds: Double): TelemetryPoint? {
        synchronized(lock) {
            if (points.isEmpty()) return null
            val lastTs = points.last().timestamp
            val cutoff = lastTs - windowSeconds

            var bestPoint: TelemetryPoint? = null
            var minDiff = Double.MAX_VALUE

            for (i in points.indices.reversed()) {
                val p = points[i]
                if (p.timestamp < cutoff) break
                val diff = abs(p.timestamp - targetTime)
                if (diff < minDiff) {
                    minDiff = diff
                    bestPoint = p
                }
            }
            return bestPoint
        }
    }

    /**
     * Calculate fast O(K) rolling window statistics matching the chart time window.
     */
    fun getWindowStats(
        windowSeconds: Double,
        outV: MetricStats,
        outI: MetricStats,
        outP: MetricStats
    ) {
        synchronized(lock) {
            outV.reset()
            outI.reset()
            outP.reset()

            if (points.isEmpty()) return

            val lastPt = points.last()
            val cutoff = lastPt.timestamp - windowSeconds

            outV.current = lastPt.voltageV
            outI.current = lastPt.currentMa
            outP.current = lastPt.powerMw

            for (i in points.indices.reversed()) {
                val p = points[i]
                if (p.timestamp < cutoff) break
                outV.updateStatsOnly(p.voltageV)
                outI.updateStatsOnly(p.currentMa)
                outP.updateStatsOnly(p.powerMw)
            }
        }
    }

    /**
     * Returns up to 3 most recent points for median display filtering.
     */
    fun getRecent(count: Int = 3): List<TelemetryPoint> {
        synchronized(lock) {
            val size = points.size
            if (size == 0) return emptyList()
            val takeCount = kotlin.math.min(count, size)
            return points.subList(size - takeCount, size).toList()
        }
    }

    fun getAllPoints(): List<TelemetryPoint> {
        synchronized(lock) {
            return ArrayList(points)
        }
    }

    fun size(): Int = synchronized(lock) { points.size }
}

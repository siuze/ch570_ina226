package com.ch570.probe.monitor.model

/**
 * Single telemetry sample point captured from BLE broadcast beacon.
 */
data class TelemetryPoint(
    val timestamp: Double,    // Relative seconds since monitor started
    val epochMs: Long,        // Wall-clock epoch milliseconds
    val voltageV: Double,     // Bus voltage in Volts (V)
    val currentMa: Double,    // Shunt current in Milliamperes (mA)
    val powerMw: Double,      // Instantaneous power in Milliwatts (mW)
    val rssi: Int,            // Received Signal Strength Indication in dBm
    val sequence: Int         // Rolling sequence number (0..255)
)

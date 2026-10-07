package com.ch570.probe.monitor.ble

import android.bluetooth.le.ScanRecord
import android.os.ParcelUuid

/**
 * Parsed payload from 0xFCD2 Service Data advertisement.
 */
data class ParsedAdvData(
    val currentCode: Int,
    val busVoltageCode: Int,
    val sequence: Int,
    val currentMa: Double,
    val voltageV: Double,
    val powerMw: Double
)

object BlePacketParser {

    val TARGET_SERVICE_UUID: ParcelUuid = ParcelUuid.fromString("0000fcd2-0000-1000-8000-00805f9b34fb")

    /**
     * Parse ScanRecord into ParsedAdvData.
     * Tries ScanRecord.serviceData first, and falls back to raw bytes TLV scanner.
     */
    fun parse(record: ScanRecord?): ParsedAdvData? {
        if (record == null) return null

        // Method 1: Check standard Android ServiceData map
        val sData = record.getServiceData(TARGET_SERVICE_UUID)
        if (sData != null && sData.size >= 5) {
            return parsePayload(sData, 0)
        }

        // Method 2: Manual scan of raw advertisement bytes
        val bytes = record.bytes ?: return null
        return parseRawBytes(bytes)
    }

    /**
     * Parse raw advertisement bytes TLV structure.
     */
    fun parseRawBytes(bytes: ByteArray): ParsedAdvData? {
        var i = 0
        while (i < bytes.size) {
            val len = bytes[i].toInt() and 0xFF
            if (len == 0 || i + 1 + len > bytes.size) break

            val type = bytes[i + 1].toInt() and 0xFF
            // 0x16: Service Data - 16-bit UUID
            if (type == 0x16 && len >= 8) {
                val uuidLo = bytes[i + 2].toInt() and 0xFF
                val uuidHi = bytes[i + 3].toInt() and 0xFF
                // 0xFCD2 (Little-endian: 0xD2, 0xFC)
                if (uuidLo == 0xD2 && uuidHi == 0xFC) {
                    val payload = ByteArray(len - 3)
                    System.arraycopy(bytes, i + 4, payload, 0, payload.size)
                    return parsePayload(payload, 0)
                }
            }
            i += 1 + len
        }
        return null
    }

    /**
     * Decode payload starting at [offset]:
     * - Bytes 0..3: low 17 bits signed current, high 15 bits bus voltage
     * - Byte 4 (optional): sequence
     */
    private fun parsePayload(data: ByteArray, offset: Int): ParsedAdvData? {
        if (data.size < offset + 4) return null

        val packed = (data[offset].toInt() and 0xFF) or
                ((data[offset + 1].toInt() and 0xFF) shl 8) or
                ((data[offset + 2].toInt() and 0xFF) shl 16) or
                ((data[offset + 3].toInt() and 0xFF) shl 24)
        var currentCode = packed and 0x1FFFF
        if ((currentCode and 0x10000) != 0) currentCode = currentCode or -0x20000
        val busVoltageCode = (packed ushr 17) and 0x7FFF

        val seq = if (data.size > offset + 4) data[offset + 4].toInt() and 0xFF else 0

        val currentMa = currentCode.toDouble() * 0.125
        val voltageV = busVoltageCode.toDouble() * 0.00125
        val powerMw = voltageV * currentMa

        return ParsedAdvData(
            currentCode = currentCode,
            busVoltageCode = busVoltageCode,
            sequence = seq,
            currentMa = currentMa,
            voltageV = voltageV,
            powerMw = powerMw
        )
    }
}

package com.ch570.probe.monitor.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow

data class BleScanEvent(
    val macAddress: String,
    val rssi: Int,
    val parsedData: ParsedAdvData,
    val timestampNanos: Long
)

class BleScanner(private val context: Context) {

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? get() = bluetoothManager?.adapter
    private val leScanner: BluetoothLeScanner? get() = bluetoothAdapter?.bluetoothLeScanner

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    private val _scanEvents = MutableSharedFlow<BleScanEvent>(extraBufferCapacity = 64)
    val scanEvents: SharedFlow<BleScanEvent> = _scanEvents.asSharedFlow()

    private val _lastError = MutableStateFlow<String?>(null)
    val lastError: StateFlow<String?> = _lastError.asStateFlow()

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult?) {
            result ?: return
            val record = result.scanRecord ?: return
            val parsed = BlePacketParser.parse(record) ?: return

            val event = BleScanEvent(
                macAddress = result.device?.address ?: "UNKNOWN",
                rssi = result.rssi,
                parsedData = parsed,
                timestampNanos = result.timestampNanos
            )
            _scanEvents.tryEmit(event)
        }

        override fun onScanFailed(errorCode: Int) {
            _isScanning.value = false
            _lastError.value = "BLE 扫描启动失败 (错误码: $errorCode)"
        }
    }

    fun isBluetoothEnabled(): Boolean {
        return bluetoothAdapter?.isEnabled == true
    }

    @SuppressLint("MissingPermission")
    fun startScan(): Boolean {
        val adapter = bluetoothAdapter
        if (adapter == null || !adapter.isEnabled) {
            _lastError.value = "请先开启系统蓝牙"
            return false
        }

        val scanner = leScanner
        if (scanner == null) {
            _lastError.value = "未获取到 BLE 扫描器"
            return false
        }

        if (_isScanning.value) return true

        try {
            // Configure Low Latency for real-time high-cadence telemetry
            val settings = ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .setReportDelay(0L)
                .build()

            // We do not set strict filters here so custom ROMs won't drop ADV_NONCONN_IND packets,
            // parsing logic filters by UUID 0xFCD2 inside onScanResult.
            val filters = listOf(
                ScanFilter.Builder()
                    .build()
            )

            scanner.startScan(filters, settings, scanCallback)
            _isScanning.value = true
            _lastError.value = null
            return true
        } catch (e: SecurityException) {
            _lastError.value = "缺少蓝牙扫描权限: ${e.message}"
            return false
        } catch (e: Exception) {
            _lastError.value = "启动扫描异常: ${e.message}"
            return false
        }
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        if (!_isScanning.value) return
        try {
            leScanner?.stopScan(scanCallback)
        } catch (_: Exception) {
        } finally {
            _isScanning.value = false
        }
    }
}

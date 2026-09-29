/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_adv.h
 * Version            : V1.0
 * Date               : 2026/09/24
 * Description        : Probe-side connectionless BLE advertising (fallback mode).
 *
 *                      When the probe is NOT bound to the PC dongle it degrades
 *                      to a BLE beacon: it hand-builds BLE ADV_NONCONN_IND frames
 *                      on the 2.4G RF IP (same libCH57xRF.a) and broadcasts them
 *                      on advertising channels 37/38/39 so any BLE scanner
 *                      (phone / nRF Connect / Home Assistant) can read telemetry
 *                      without a connection.
 *
 *                      Approach follows the public MIT-licensed reference
 *                      "CH570Q_BTHome_Prod" (github.com/ALittleJerry/WCH_TH_CH570):
 *                      access address 0x8E89BED6, CRC24 init 0x555555 / poly
 *                      0x80032D appended by hardware, per-channel whitening by
 *                      hardware, 1M PHY, LSB-first.
 *******************************************************************************/
#ifndef __BLE_ADV_H
#define __BLE_ADV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CH57x_common.h"

/* BLE advertising channels (RF channel index as accepted by the RF lib) */
#define BLE_ADV_CH37            37      /* 2402 MHz */
#define BLE_ADV_CH38            38      /* 2426 MHz */
#define BLE_ADV_CH39            39      /* 2480 MHz */

/* Maximum ADV data bytes in a single ADV_NONCONN_IND PDU */
#define BLE_ADV_DATA_MAX        31

/* Advertising interval while degraded (ms) */
#define BLE_ADV_PERIOD_MS       1000

/* How long the probe must stay unbound before it degrades to beacon mode (ms) */
#define BLE_ADV_DEGRADE_MS      3000

/**
 * @brief  Initialise the beacon module (call once after RFRole_Init).
 * @param  addr  6-byte advertiser address (AdvA), LSB-first on air.
 */
void BLE_AdvInit(const uint8_t addr[6]);

/**
 * @brief  Refresh the advertised payload with raw INA226 resolution (Current & Voltage only).
 * @param  shunt_raw   INA226 raw shunt voltage register (int16_t, 1 LSB = 0.125mA with 20mOhm)
 * @param  bus_raw     INA226 raw bus voltage register (uint16_t, 1 LSB = 1.25mV)
 */
void BLE_AdvSetTelemetryRaw(int16_t shunt_raw, uint16_t bus_raw);

/**
 * @brief  Refresh the advertised payload (backward-compatible wrapper).
 */
void BLE_AdvSetTelemetry(int32_t current_ma, uint16_t bus_mv, uint32_t power_mw);

/**
 * @brief  Transmit one advertising burst on channels 37, 38 and 39 (blocking).
 */
void BLE_AdvBurst(void);

/* --- integration hooks used by the proprietary-link state machine --- */
uint8_t BLE_AdvActive(void);   /* non-zero while an ADV burst owns the RF IP */
void    BLE_AdvMarkDone(void); /* called from RF TX-finish / timeout callback */

#ifdef __cplusplus
}
#endif

#endif /* __BLE_ADV_H */

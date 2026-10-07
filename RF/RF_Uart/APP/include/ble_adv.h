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

/* BLE primary advertising channels (RF MHz offset + whitening index) */
#define BLE_ADV_CH37_FREQ       37      /* RFIP BLE channel 37 = 2402 MHz */
#define BLE_ADV_CH38_FREQ       38      /* RFIP BLE channel 38 = 2426 MHz */
#define BLE_ADV_CH39_FREQ       39      /* RFIP BLE channel 39 = 2480 MHz */
#define BLE_ADV_CH37_IDX        37
#define BLE_ADV_CH38_IDX        38
#define BLE_ADV_CH39_IDX        39

/* Maximum ADV data bytes in a single ADV_NONCONN_IND PDU */
#define BLE_ADV_DATA_MAX        31

/* Default/configurable advertising rate while unbound. */
#define BLE_ADV_DEFAULT_HZ      4
#define BLE_ADV_MIN_HZ          1
#define BLE_ADV_MAX_HZ          20

/* How long the probe must stay unbound before it degrades to beacon mode (ms) */
#define BLE_ADV_DEGRADE_MS      1000

/**
 * @brief  Initialise the beacon module (call once after RFRole_Init).
 * @param  addr  6-byte advertiser address (AdvA), LSB-first on air.
 */
void BLE_AdvInit(const uint8_t addr[6]);

/**
 * @brief  Refresh the advertised payload with compressed engineering values.
 * @param  current_ua  signed current in microamps; encoded in a signed
 *                     17-bit field at 0.125mA/LSB (about +/-8.192A).
 * @param  bus_uv      bus voltage in microvolts; encoded in a 15-bit field at
 *                     1.25mV/LSB (0..40.95875V).
 */
void BLE_AdvSetTelemetryFixed(int32_t current_ua, uint32_t bus_uv, uint32_t power_mw);

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

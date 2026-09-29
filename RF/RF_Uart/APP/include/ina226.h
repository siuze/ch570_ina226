/********************************** (C) COPYRIGHT *******************************
 * File Name          : ina226.h
 * Author             : Antigravity
 * Version            : V1.0
 * Date               : 2026/09/23
 * Description        : INA226 I2C driver for CH570Q (Fast Mode 400kHz)
 *******************************************************************************/

#ifndef __INA226_H__
#define __INA226_H__

#include <stdint.h>
#include "CH57x_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* INA226 7-bit I2C Address (A0=GND, A1=GND -> 0x40, shifted for 8-bit: 0x80) */
#define INA226_I2C_ADDR         0x40

/* INA226 Register Addresses */
#define INA226_REG_CONFIG       0x00
#define INA226_REG_SHUNTVOLTAGE 0x01
#define INA226_REG_BUSVOLTAGE   0x02
#define INA226_REG_POWER        0x03
#define INA226_REG_CURRENT      0x04
#define INA226_REG_CALIBRATION  0x05
#define INA226_REG_MASKENABLE   0x06
#define INA226_REG_ALERTLIMIT   0x07
#define INA226_REG_MANUF_ID     0xFE
#define INA226_REG_DIE_ID       0xFF

/* I2C Pin definitions on CH570Q */
#define INA226_SCL_PIN          (1 << 3)    // PA3 (netlist U105 pin5=SCL)
#define INA226_SDA_PIN          (1 << 2)    // PA2 (netlist U105 pin4=SDA)

/* Averaging options (bits 11-9 in Config Reg) */
#define INA226_AVG_1            0x0000
#define INA226_AVG_4            0x0200
#define INA226_AVG_16           0x0400
#define INA226_AVG_64           0x0600
#define INA226_AVG_128          0x0800
#define INA226_AVG_256          0x0A00
#define INA226_AVG_512          0x0C00
#define INA226_AVG_1024         0x0E00

/* Vbus / Vshunt conversion time: 1.1ms (bits 8-6, 5-3 = 100b) */
#define INA226_VBUS_1100US      (4 << 6)
#define INA226_VSHUNT_1100US    (4 << 3)
#define INA226_MODE_CONT_SHUNT_BUS 0x0007   // Continuous shunt and bus voltage

/* Structure holding measurement results */
typedef struct {
    int32_t current_ma;     // Current in mA (signed)
    uint32_t bus_mv;        // Bus Voltage in mV
    uint32_t power_mw;      // Power in mW
    int16_t shunt_raw;      // Raw shunt register (2.5uV / LSB)
    uint16_t bus_raw;       // Raw bus register (1.25mV / LSB)
} ina226_data_t;

/* Functions */
void INA226_Init(uint32_t r_shunt_uohm, uint16_t avg_samples);
uint8_t INA226_ReadData(ina226_data_t *pData);
void INA226_SetConfig(uint32_t r_shunt_uohm, uint16_t avg_samples);
uint32_t INA226_GetShuntResistance(void);

#ifdef __cplusplus
}
#endif

#endif /* __INA226_H__ */

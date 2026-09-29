/********************************** (C) COPYRIGHT *******************************
 * File Name          : ina226.c
 * Author             : Antigravity
 * Version            : V1.0
 * Date               : 2026/09/23
 * Description        : INA226 Fast Mode (400kHz) I2C Driver for CH570Q
 *******************************************************************************/

#include "ina226.h"

static uint32_t s_shunt_uohm = 20000;   // Default 20 mOhm (20,000 uOhm, matches R104)
static uint16_t s_avg_cfg = INA226_AVG_16;

/* I2C Low-level bit-bang timing for 400kHz */
static inline void i2c_delay(void)
{
    /* At 24MHz HCLK, ~20-30 NOPs gives ~1-1.2us half-cycle (~400kHz) */
    __asm volatile (
        "nop; nop; nop; nop; nop; nop; nop; nop;\n"
        "nop; nop; nop; nop; nop; nop; nop; nop;\n"
        "nop; nop; nop; nop; nop; nop; nop; nop;\n"
    );
}

static inline void scl_high(void)
{
    GPIOA_ModeCfg(INA226_SCL_PIN, GPIO_ModeIN_PU);
}

static inline void scl_low(void)
{
    GPIOA_ResetBits(INA226_SCL_PIN);
    GPIOA_ModeCfg(INA226_SCL_PIN, GPIO_ModeOut_PP_5mA);
}

static inline void sda_high(void)
{
    GPIOA_ModeCfg(INA226_SDA_PIN, GPIO_ModeIN_PU);
}

static inline void sda_low(void)
{
    GPIOA_ResetBits(INA226_SDA_PIN);
    GPIOA_ModeCfg(INA226_SDA_PIN, GPIO_ModeOut_PP_5mA);
}

static inline uint8_t sda_read(void)
{
    return (GPIOA_ReadPortPin(INA226_SDA_PIN) != 0) ? 1 : 0;
}

static void i2c_start(void)
{
    sda_high();
    scl_high();
    i2c_delay();
    sda_low();
    i2c_delay();
    scl_low();
    i2c_delay();
}

static void i2c_stop(void)
{
    sda_low();
    i2c_delay();
    scl_high();
    i2c_delay();
    sda_high();
    i2c_delay();
}

static uint8_t i2c_write_byte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        if (byte & 0x80)
            sda_high();
        else
            sda_low();
        byte <<= 1;
        i2c_delay();
        scl_high();
        i2c_delay();
        scl_low();
        i2c_delay();
    }
    
    /* Read ACK */
    sda_high();
    i2c_delay();
    scl_high();
    i2c_delay();
    uint8_t ack = sda_read();
    scl_low();
    i2c_delay();
    return (ack == 0); // 1 = ACK, 0 = NAK
}

static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t i, byte = 0;
    sda_high();
    for (i = 0; i < 8; i++)
    {
        byte <<= 1;
        scl_high();
        i2c_delay();
        if (sda_read())
            byte |= 0x01;
        scl_low();
        i2c_delay();
    }
    
    /* Send ACK or NAK */
    if (ack)
        sda_low();
    else
        sda_high();
    i2c_delay();
    scl_high();
    i2c_delay();
    scl_low();
    sda_high();
    i2c_delay();
    return byte;
}

static uint8_t ina226_write_reg(uint8_t reg, uint16_t value)
{
    i2c_start();
    if (!i2c_write_byte(INA226_I2C_ADDR << 1)) {
        i2c_stop();
        return 0;
    }
    if (!i2c_write_byte(reg)) {
        i2c_stop();
        return 0;
    }
    if (!i2c_write_byte((uint8_t)(value >> 8))) {
        i2c_stop();
        return 0;
    }
    if (!i2c_write_byte((uint8_t)(value & 0xFF))) {
        i2c_stop();
        return 0;
    }
    i2c_stop();
    return 1;
}

static uint8_t ina226_read_reg(uint8_t reg, uint16_t *pValue)
{
    i2c_start();
    if (!i2c_write_byte(INA226_I2C_ADDR << 1)) {
        i2c_stop();
        return 0;
    }
    if (!i2c_write_byte(reg)) {
        i2c_stop();
        return 0;
    }
    
    i2c_start(); // Repeated START
    if (!i2c_write_byte((INA226_I2C_ADDR << 1) | 0x01)) {
        i2c_stop();
        return 0;
    }
    
    uint8_t msb = i2c_read_byte(1); // ACK
    uint8_t lsb = i2c_read_byte(0); // NAK
    i2c_stop();
    
    *pValue = ((uint16_t)msb << 8) | lsb;
    return 1;
}

void INA226_SetConfig(uint32_t r_shunt_uohm, uint16_t avg_samples)
{
    if (r_shunt_uohm > 0)
        s_shunt_uohm = r_shunt_uohm;
    
    switch (avg_samples) {
        case 1:    s_avg_cfg = INA226_AVG_1; break;
        case 4:    s_avg_cfg = INA226_AVG_4; break;
        case 16:   s_avg_cfg = INA226_AVG_16; break;
        case 64:   s_avg_cfg = INA226_AVG_64; break;
        case 128:  s_avg_cfg = INA226_AVG_128; break;
        case 256:  s_avg_cfg = INA226_AVG_256; break;
        case 512:  s_avg_cfg = INA226_AVG_512; break;
        case 1024: s_avg_cfg = INA226_AVG_1024; break;
        default:   s_avg_cfg = INA226_AVG_16; break;
    }
    
    /* Config register: Avg + Vbus 1.1ms + Vshunt 1.1ms + Cont Shunt & Bus */
    uint16_t cfg_val = s_avg_cfg | INA226_VBUS_1100US | INA226_VSHUNT_1100US | INA226_MODE_CONT_SHUNT_BUS;
    ina226_write_reg(INA226_REG_CONFIG, cfg_val);
}

void INA226_Init(uint32_t r_shunt_uohm, uint16_t avg_samples)
{
    /* Configure pins: PA2 (SDA), PA3 (SCL) with internal pull-up idle HIGH */
    scl_high();
    sda_high();
    i2c_delay();
    
    INA226_SetConfig(r_shunt_uohm, avg_samples);
}

uint32_t INA226_GetShuntResistance(void)
{
    return s_shunt_uohm;
}

uint8_t INA226_ReadData(ina226_data_t *pData)
{
    uint16_t vshunt_raw = 0;
    uint16_t vbus_raw = 0;
    
    if (!ina226_read_reg(INA226_REG_SHUNTVOLTAGE, &vshunt_raw))
        return 0;
    if (!ina226_read_reg(INA226_REG_BUSVOLTAGE, &vbus_raw))
        return 0;
        
    pData->shunt_raw = (int16_t)vshunt_raw;
    pData->bus_raw = vbus_raw;
    
    /* Bus Voltage: 1.25 mV / LSB -> mV = raw * 5 / 4 */
    pData->bus_mv = ((uint32_t)vbus_raw * 5) / 4;
    
    /* Shunt Voltage: 2.5 uV / LSB -> Shunt_uV = raw * 25 / 10 = raw * 5 / 2 */
    int32_t shunt_uv = ((int32_t)pData->shunt_raw * 5) / 2;
    
    /* Current (mA) = (Shunt_uV * 1000) / s_shunt_uohm */
    if (s_shunt_uohm > 0)
    {
        pData->current_ma = (int32_t)(((int64_t)shunt_uv * 1000) / s_shunt_uohm);
    }
    else
    {
        pData->current_ma = 0;
    }
    
    /* Power (mW) = (Current_mA * Bus_mV) / 1000 (absolute current for power) */
    int32_t abs_curr = (pData->current_ma < 0) ? -pData->current_ma : pData->current_ma;
    pData->power_mw = (uint32_t)(((uint64_t)abs_curr * pData->bus_mv) / 1000);
    
    return 1;
}

/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_uart.h
 * Author             : WCH / Modified for Wireless Current Sensor Tool
 * Version            : V2.0
 * Date               : 2026/09/23
 * Description        : Dual CDC-ACM (COM1 MCU Passthrough, COM2 Telemetry & Control)
 *******************************************************************************/

#ifndef __USB_UART_H
#define __USB_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CH57x_common.h"
#include "buf.h"
#include "rf.h"

#define LED_PIN             (1 << 3) // PA3: Link / Comm-status LED (schematic LED202)
#define LED2_PIN            (1 << 2) // PA2: Current Indicator (schematic LED201)
#define LINK_LED_MIN_DUTY   26u       // 10% of the 8-bit full-scale duty
#define LED_PWM_CYCLE       52083u    // 100MHz / 8 / (52083+1) ~= 240Hz
#define LED_PWM_MIN_DUTY    2050u     // about 3.1% of 16-bit scale

typedef struct __PACKED _LINE_CODE
{
    uint32_t BaudRate;
    uint8_t  StopBits;
    uint8_t  ParityType;
    uint8_t  DataBits;
    uint8_t  ioStaus;
} LINE_CODE, *PLINE_CODE;

extern LINE_CODE Uart0Para;
extern uint8_t   UART_Status;
extern volatile uint16_t g_current_duty;
extern volatile uint32_t g_current_report_tick;
extern volatile uint32_t g_com1_activity_bytes;
extern uint8_t volatile RF_bound_Flag;
extern dev_config_t g_dongle_cfg;

void USB_Init(void);
void USB_StatusQuery(void);

/* COM1: MCU Passthrough API */
uint8_t COM1_RxPop(uint8_t *byte);
uint8_t USB_RxQuery(void *buf, typeBufSize *len);
void    COM1_SendBytes(const uint8_t *data, uint16_t len);
uint8_t USB_FirmwareUpdateActive(void);

/* COM2: Telemetry & Control API */
void    COM2_SendBytes(const char *data, uint16_t len);
uint8_t COM2_PopPendingCmd(void *cmd);

/* LED2 hardware PWM & 5s heartbeat */
void LED2_Init(void);
void LED2_Poll(void);

/* LED1 link / comm-status indicator */
void LED1_Poll(void);
void LED1_Init(void);

/* Probe Wireless OTA API */
extern volatile uint8_t s_probe_ota_pending;
extern ota_cmd_pkt_t    s_probe_ota_cmd;
extern volatile uint8_t s_probe_ota_acked;
extern ota_rsp_pkt_t    s_probe_ota_rsp;

#ifdef __cplusplus
}
#endif

#endif /* __USB_UART_H */

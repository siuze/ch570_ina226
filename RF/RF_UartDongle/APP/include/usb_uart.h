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
#define LED2_PWM_PIN        (1 << 2) // PA2: Current Indicator PWM2 (schematic LED201)

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
extern uint8_t   g_telemetry_in_5s;
extern uint8_t   g_current_duty;
extern uint8_t volatile RF_bound_Flag;
extern dev_config_t g_dongle_cfg;

void USB_Init(void);
void USB_StatusQuery(void);

/* COM1: MCU Passthrough API */
uint8_t COM1_RxPop(uint8_t *byte);
uint8_t USB_RxQuery(void *buf, typeBufSize *len);
void    COM1_SendBytes(const uint8_t *data, uint16_t len);

/* COM2: Telemetry & Control API */
void    COM2_SendBytes(const char *data, uint16_t len);
uint8_t COM2_HasPendingCmd(void);
ctrl_cmd_pkt_t* COM2_GetPendingCmd(void);
void    COM2_ClearPendingCmd(void);

/* LED2 PWM & 5s Heartbeat */
void LED2_PWM_Init(void);
void LED2_Poll(void);

/* LED1 link / comm-status indicator */
void LED1_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_UART_H */

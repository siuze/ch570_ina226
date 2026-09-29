/********************************** (C) COPYRIGHT *******************************
 * File Name          : uart.h
 * Author             : WCH / Modified for Wireless Current Sensor Tool
 * Version            : V2.0
 * Date               : 2026/09/23
 * Description        : UART driver with dynamic pin swapping and MAX811S control
 *******************************************************************************/

#ifndef BLE_DIRECTTEST_APP_INCLUDE_UART_H
#define BLE_DIRECTTEST_APP_INCLUDE_UART_H

#include "buf.h"
#include "CH57x_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optimized buffer length to leave plenty of RAM (>2KB) for stack */
#define UART_BUF_LEN        (1024 * 2)

#define TXD_PIN_PA0         (1 << 0)  // PA0
#define RXD_PIN_PA1         (1 << 1)  // PA1

/* MAX811S Control Pin on PA7 */
#define MAX811_PIN          (1 << 7)  // PA7

enum uart_status
{
    UART_STATUS_IDLE,
    UART_STATUS_START,
    UART_STATUS_RCVING,
    UART_STATUS_RCV_END,
    UART_STATUS_SENDING,
    UART_STATUS_SEND,
    UART_STATUS_NUM,
};

#define DATA_LEN_UART       (32)

extern uint8_t gBoundStatus;

void UART_Init(void);
uint8_t UART_RxQuery(void *buf, typeBufSize *len);
void UART_SetBuad(uint32_t buad);
void UART_SetTimer(uint16_t ms);
void UART_Send(char *data, uint16_t size);

/* Pin swap: 0 = Normal (PA0 TX, PA1 RX), 1 = Swapped (PA1 TX, PA0 RX) */
void UART_SwapPins(uint8_t swap);
uint8_t UART_IsSwapped(void);

/* Target Reset & Bootloader via MAX811S on PA7 */
void MAX811_Init(void);
void MAX811_ResetTarget(void);
void MAX811_BootloaderTarget(void);
void MAX811_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_DIRECTTEST_APP_INCLUDE_UART_H */

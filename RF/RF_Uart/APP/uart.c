/********************************** (C) COPYRIGHT *******************************
 * File Name          : uart.c
 * Author             : WCH / Modified for Wireless Current Sensor Tool
 * Version            : V2.0
 * Date               : 2026/09/23
 * Description        : UART driver with dynamic pin swapping and MAX811S control
 *******************************************************************************/

#include "CH57x_common.h"
#include "uart.h"
#include "rf_uart_tx.h"

static uint8_t uart_buf[UART_BUF_LEN];
static struct simple_buf *pUartbuf = NULL;
static struct simple_buf uart_buffer;

volatile uint8_t uart_flag;
uint32_t gBaudRate;
uint32_t gSysClock;
uint32_t gIntervalTimer;
uint32_t gUartRxCount;

static uint8_t s_uart_swapped = 0;

/* MAX811S Pulse state machine */
static volatile uint32_t s_max811_pulse_ms = 0;

#define BOUND_GET_PERI   10

static void uart_buffer_create(struct simple_buf **buf)
{
    *buf = simple_buf_create(&uart_buffer, uart_buf, sizeof(uart_buf));
}

__HIGH_CODE
static void uart_rx_timeout(void)
{
    R32_TMR_CNT_END = (R16_UART_DL * 8) * 100; // 100 bit time
    R8_TMR_CTRL_MOD = RB_TMR_ALL_CLEAR;
    R8_TMR_CTRL_MOD = RB_TMR_COUNT_EN;
}

__INTERRUPT
__HIGH_CODE
void TMR_IRQHandler(void)
{
    if (TMR_GetITFlag(TMR_IT_CYC_END))
    {
        TMR_ClearITFlag(TMR_IT_CYC_END);
        R32_TMR_CNT_END = gIntervalTimer;
        R8_TMR_CTRL_MOD = RB_TMR_ALL_CLEAR;
        R8_TMR_CTRL_MOD = RB_TMR_COUNT_EN;
        if (uart_flag == UART_STATUS_START)
        {
            uart_flag = UART_STATUS_SENDING;
        }
        else if (uart_flag == UART_STATUS_RCVING)
        {
            uart_flag = UART_STATUS_RCV_END;
        }
        else if (uart_flag == UART_STATUS_SENDING)
        {
            uart_flag = UART_STATUS_SEND;
        }
    }
}

__HIGH_CODE
void UART_SetTimer(uint16_t ms)
{
    gSysClock = GetSysClock();
    gIntervalTimer = gSysClock / 2000 * ms;
    R8_TMR_CTRL_MOD = RB_TMR_ALL_CLEAR;
    R8_TMR_CTRL_MOD = RB_TMR_COUNT_EN;
}

__HIGH_CODE
void UART_SetBuad(uint32_t buad)
{
    if (gBaudRate != buad)
    {
        uint32_t x;
        gSysClock = GetSysClock();
        PRINT("bsp = %d\n", buad);
        mDelaymS(1);
        gBaudRate = buad;
        x = 10 * gSysClock / 8 / buad;
        x = (x + 5) / 10;
        R16_UART_DL = (uint16_t)x;
    }
}

__HIGH_CODE
uint8_t UART_RxQuery(void *buf, typeBufSize *len)
{
    if (uart_flag == UART_STATUS_RCV_END)
    {
        if (read_buf(pUartbuf, buf, len) == 0)
        {
            PFIC_DisableIRQ(UART_IRQn);
            uart_flag = UART_STATUS_START;
            PFIC_EnableIRQ(UART_IRQn);
        }
        if (*len) return 0;
        else return 0xFF;
    }
    else if (uart_flag == UART_STATUS_SEND)
    {
        *len = 0;
        PFIC_DisableIRQ(UART_IRQn);
        uart_flag = UART_STATUS_START;
        PFIC_EnableIRQ(UART_IRQn);
        return 0x80;
    }
    else
    {
        *len = 0;
        return 0xFF;
    }
}

__HIGH_CODE
void UART_Send(char *data, uint16_t size)
{
    for (int i = 0; i < size; i++)
    {
        while (R8_UART_TFC == UART_FIFO_SIZE);
        R8_UART_THR = *data++;
    }
}

__INTERRUPT
__HIGH_CODE
void UART_IRQHandler(void)
{
    uint8_t i;
    uint8_t tmp_buf[UART_FIFO_SIZE];
    typeBufSize len;

    switch (UART_GetITFlag())
    {
        case UART_II_LINE_STAT:
        {
            (void)UART_GetLinSTA();
            break;
        }

        case UART_II_RECV_RDY:
            len = R8_UART_RFC;
            i = len;
            do {
                tmp_buf[len - i] = UART_RecvByte();
            } while (--i);
            if (write_buf(pUartbuf, tmp_buf, &len) >= DATA_LEN_UART)
            {
                uart_flag = UART_STATUS_RCV_END;
            }
            else
            {
                uart_flag = UART_STATUS_RCVING;
                uart_rx_timeout();
            }
            gUartRxCount += len;
            break;

        case UART_II_RECV_TOUT:
            len = R8_UART_RFC;
            i = len;
            do {
                tmp_buf[len - i] = UART_RecvByte();
            } while (--i);
            write_buf(pUartbuf, tmp_buf, &len);
            gUartRxCount += len;
            uart_flag = UART_STATUS_RCV_END;
            break;

        default:
            if (gRfRxFlag)
            {
                len = UART_FIFO_SIZE - R8_UART_TFC;
                gRfRxFlag = read_buf(pRfBuf, tmp_buf, &len);
                for (int j = 0; j < len; j++)
                {
                    R8_UART_THR = tmp_buf[j];
                }
            }
            break;
    }
}

int uart_start_receiving(void)
{
    uart_flag = UART_STATUS_START;
    uart_buffer_create(&pUartbuf);
    PFIC_EnableIRQ(UART_IRQn);

    UART_SetTimer(ADV_INTERVAL);
    R8_TMR_CTRL_MOD = RB_TMR_COUNT_EN;
    TMR_ITCfg(ENABLE, TMR_IT_CYC_END);
    PFIC_EnableIRQ(TMR_IRQn);
    return 0;
}

void UART_SwapPins(uint8_t swap)
{
    s_uart_swapped = swap ? 1 : 0;
    
    PFIC_DisableIRQ(UART_IRQn);
    if (!s_uart_swapped)
    {
        /* Normal: PA0=TX, PA1=RX */
        GPIOA_SetBits(TXD_PIN_PA0);
        GPIOA_ModeCfg(TXD_PIN_PA0, GPIO_ModeOut_PP_5mA);
        GPIOA_ModeCfg(RXD_PIN_PA1, GPIO_ModeIN_PU);
        UART_Remap(ENABLE, UART_TX_REMAP_PA0, UART_RX_REMAP_PA1);
    }
    else
    {
        /* Swapped: PA1=TX, PA0=RX */
        GPIOA_SetBits(RXD_PIN_PA1);
        GPIOA_ModeCfg(RXD_PIN_PA1, GPIO_ModeOut_PP_5mA);
        GPIOA_ModeCfg(TXD_PIN_PA0, GPIO_ModeIN_PU);
        UART_Remap(ENABLE, UART_TX_REMAP_PA1, UART_RX_REMAP_PA0);
    }
    PFIC_EnableIRQ(UART_IRQn);
    PRINT("UART Pins Swapped=%d\n", s_uart_swapped);
}

uint8_t UART_IsSwapped(void)
{
    return s_uart_swapped;
}

/* MAX811S Control on PA7 */
void MAX811_Init(void)
{
    /* Idle state: Input with pull-up (Hi-Z), allowing user button and internal pull-ups to be undisturbed */
    GPIOA_SetBits(MAX811_PIN);
    GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeIN_PU);
    s_max811_pulse_ms = 0;
}

void MAX811_ResetTarget(void)
{
    /* Normal Reset: 20ms pulse LOW.
     * C1 discharges and pulses /MR, MAX811S latches reset (Trp min=85ms, typ=500ms).
     * PA7 returns high at 20ms, so GPIO0 is high when EN rises at 500ms -> Normal Boot!
     */
    GPIOA_ResetBits(MAX811_PIN);
    GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeOut_PP_5mA);
    mDelaymS(20);
    GPIOA_SetBits(MAX811_PIN);
    GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeIN_PU);
    PRINT("Target Reset Triggered\n");
}

void MAX811_BootloaderTarget(void)
{
    /* Bootloader Reset: 1100ms pulse LOW.
     * C1 pulses /MR at t=0, MSKSEMI MAX811S releases EN at t=500ms~900ms.
     * PA7 is held LOW until t=1100ms (> Trp_max 900ms), so GPIO0 is guaranteed LOW
     * at EN rising edge -> Target latches Bootloader Mode reliably!
     */
    GPIOA_ResetBits(MAX811_PIN);
    GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeOut_PP_5mA);
    mDelaymS(1100);
    GPIOA_SetBits(MAX811_PIN);
    GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeIN_PU);
    PRINT("Target Bootloader Triggered\n");
}

void MAX811_Poll(void)
{
    /* Handled synchronously in commands or polled */
}

void UART_Init(void)
{
    gSysClock = GetSysClock();

    /* Disable SWD to allow PA0/PA1 to be used as full GPIO/UART */
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;

    MAX811_Init();

    /* Default UART configuration */
    UART_SwapPins(0);

    UART_DefInit();
    UART_SetBuad(115200);

    UART_ByteTrigCfg(UART_4BYTE_TRIG);
    UART_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT | RB_IER_THR_EMPTY);
    uart_flag = UART_STATUS_IDLE;
    uart_start_receiving();
}

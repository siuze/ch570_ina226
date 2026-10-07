/********************************** (C) COPYRIGHT
 * ******************************* File Name          : uart.c Author   : WCH /
 * Modified for Wireless Current Sensor Tool Version            : V2.0 Date
 *          : 2026/09/23 Description        : UART driver with dynamic pin
 * swapping and MAX811S control
 *******************************************************************************/

#include "uart.h"
#include "CH57x_common.h"
#include "rf.h"
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

/* PA1 drives the Probe RX activity LED (active low).  Keep the UART and its
 * interrupt disabled during this indication so target bytes cannot be
 * consumed before RF/UART initialization is complete. */
#define UART_STARTUP_RX_LED_MS 3000u

static void uart_startup_rx_led(void) {
  GPIOA_ResetBits(RXD_PIN_PA1);
  GPIOA_ModeCfg(RXD_PIN_PA1, GPIO_ModeOut_PP_5mA);
  mDelaymS(UART_STARTUP_RX_LED_MS);
  GPIOA_SetBits(RXD_PIN_PA1);
  GPIOA_ModeCfg(RXD_PIN_PA1, GPIO_ModeIN_PU);
}

/* MAX811S reset timing.  RESET# release is specified up to about 900 ms;
 * keep BOOT asserted with margin, then settle C106 before returning PA7 to a
 * genuinely floating input. */
#define MAX811_RST_TRIGGER_MS 20u
#define MAX811_PRECHARGE_MS 5u
#define MAX811_BOOT_HOLD_MS 1500u
#define MAX811_RELEASE_SETTLE_MS 200u

typedef enum {
  MAX811_SEQ_IDLE = 0,
  MAX811_SEQ_RESET_PRECHARGE,
  MAX811_SEQ_RESET_LOW,
  MAX811_SEQ_RESET_SETTLE,
  MAX811_SEQ_BOOT_PRECHARGE,
  MAX811_SEQ_BOOT_LOW,
  MAX811_SEQ_BOOT_SETTLE,
} max811_seq_t;

static max811_seq_t s_max811_seq = MAX811_SEQ_IDLE;
static uint32_t s_max811_deadline = 0;

static uint32_t max811_ms_ticks(uint32_t ms) {
  uint32_t clock = GetSysClock();
  uint32_t ticks = clock / 1000u;
  if (ticks == 0) ticks = 1;
  return ticks * ms;
}

static void max811_wait(uint32_t ms) {
  s_max811_deadline = SYS_GetSysTickCnt() + max811_ms_ticks(ms);
}

static uint8_t max811_due(uint32_t now) {
  return ((int32_t)(now - s_max811_deadline) >= 0) ? 1 : 0;
}

#define BOUND_GET_PERI 10

static void uart_buffer_create(struct simple_buf **buf) {
  *buf = simple_buf_create(&uart_buffer, uart_buf, sizeof(uart_buf));
}

__HIGH_CODE
static void uart_rx_timeout(void) {
  R32_TMR_CNT_END = (R16_UART_DL * 8) * 100; // 100 bit time
  R8_TMR_CTRL_MOD = RB_TMR_ALL_CLEAR;
  R8_TMR_CTRL_MOD = RB_TMR_COUNT_EN;
}

__INTERRUPT
__HIGH_CODE
void TMR_IRQHandler(void) {
  if (TMR_GetITFlag(TMR_IT_CYC_END)) {
    TMR_ClearITFlag(TMR_IT_CYC_END);
    R32_TMR_CNT_END = gIntervalTimer;
    R8_TMR_CTRL_MOD = RB_TMR_ALL_CLEAR;
    R8_TMR_CTRL_MOD = RB_TMR_COUNT_EN;
    if (uart_flag == UART_STATUS_START) {
      uart_flag = UART_STATUS_SENDING;
    } else if (uart_flag == UART_STATUS_RCVING) {
      uart_flag = UART_STATUS_RCV_END;
    } else if (uart_flag == UART_STATUS_SENDING) {
      uart_flag = UART_STATUS_SEND;
    }
  }
}

void UART_SetTimer(uint16_t ms) {
  gSysClock = GetSysClock();
  gIntervalTimer = gSysClock / 2000 * ms;
  R32_TMR_CNT_END = gIntervalTimer;
  R8_TMR_CTRL_MOD = RB_TMR_ALL_CLEAR;
  R8_TMR_CTRL_MOD = RB_TMR_COUNT_EN;
}

void UART_SetBuad(uint32_t buad) {
  if (gBaudRate != buad) {
    uint32_t x;
    gSysClock = GetSysClock();
    mDelaymS(1);
    gBaudRate = buad;
    x = 10 * gSysClock / 8 / buad;
    x = (x + 5) / 10;
    R16_UART_DL = (uint16_t)x;
  }
}

uint8_t UART_RxQuery(void *buf, typeBufSize *len) {
  if (uart_flag == UART_STATUS_RCV_END) {
    if (read_buf(pUartbuf, buf, len) == 0) {
      PFIC_DisableIRQ(UART_IRQn);
      uart_flag = UART_STATUS_START;
      PFIC_EnableIRQ(UART_IRQn);
    }
    if (*len)
      return 0;
    else
      return 0xFF;
  } else if (uart_flag == UART_STATUS_SEND) {
    *len = 0;
    PFIC_DisableIRQ(UART_IRQn);
    uart_flag = UART_STATUS_START;
    PFIC_EnableIRQ(UART_IRQn);
    return 0x80;
  } else {
    *len = 0;
    return 0xFF;
  }
}

__HIGH_CODE
void UART_Send(char *data, uint16_t size) {
  for (int i = 0; i < size; i++) {
    while (R8_UART_TFC == UART_FIFO_SIZE)
      ;
    R8_UART_THR = *data++;
  }
}

__INTERRUPT
__HIGH_CODE
void UART_IRQHandler(void) {
  uint8_t i;
  uint8_t tmp_buf[UART_FIFO_SIZE];
  typeBufSize len;

  switch (UART_GetITFlag()) {
  case UART_II_LINE_STAT: {
    (void)UART_GetLinSTA();
    break;
  }

  case UART_II_RECV_RDY:
    len = R8_UART_RFC;
    i = len;
    do {
      tmp_buf[len - i] = UART_RecvByte();
    } while (--i);
    if (write_buf(pUartbuf, tmp_buf, &len) >= DATA_LEN_UART) {
      uart_flag = UART_STATUS_RCV_END;
    } else {
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

  case UART_II_THR_EMPTY:
  default:
    if (gRfRxFlag) {
      len = UART_FIFO_SIZE - R8_UART_TFC;
      if (pRfBuf->read == pRfBuf->end)
        pRfBuf->read = pRfBuf->start;
      if (pRfBuf->write == pRfBuf->end)
        pRfBuf->write = pRfBuf->start;
      gRfRxFlag = read_buf(pRfBuf, tmp_buf, &len);
      for (int j = 0; j < len; j++) {
        R8_UART_THR = tmp_buf[j];
      }
      if (gRfRxFlag) {
        R8_UART_IER |= RB_IER_THR_EMPTY;
      } else {
        R8_UART_IER &= ~RB_IER_THR_EMPTY;
      }
      extern uint8_t getDataProbe;
      if ((pRfBuf->buf_len - pRfBuf->data_len) >= 128) {
        getDataProbe = 6;
      }
    } else {
      /* Safety disarm: If THR_EMPTY fired without data, clear it to prevent ISR
       * storm */
      R8_UART_IER &= ~RB_IER_THR_EMPTY;
    }
    break;
  }
}

int uart_start_receiving(void) {
  uart_flag = UART_STATUS_START;
  uart_buffer_create(&pUartbuf);
  PFIC_EnableIRQ(UART_IRQn);

  UART_SetTimer(ADV_INTERVAL);
  R8_TMR_CTRL_MOD = RB_TMR_COUNT_EN;
  TMR_ITCfg(ENABLE, TMR_IT_CYC_END);
  PFIC_EnableIRQ(TMR_IRQn);
  return 0;
}

void UART_SwapPins(uint8_t swap) {
  s_uart_swapped = swap ? 1 : 0;

  PFIC_DisableIRQ(UART_IRQn);
  if (!s_uart_swapped) {
    /* Normal: PA0=TX, PA1=RX */
    GPIOA_SetBits(TXD_PIN_PA0);
    GPIOA_ModeCfg(TXD_PIN_PA0, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(RXD_PIN_PA1, GPIO_ModeIN_PU);
    UART_Remap(ENABLE, UART_TX_REMAP_PA0, UART_RX_REMAP_PA1);
  } else {
    /* Swapped: PA1=TX, PA0=RX */
    GPIOA_SetBits(RXD_PIN_PA1);
    GPIOA_ModeCfg(RXD_PIN_PA1, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(TXD_PIN_PA0, GPIO_ModeIN_PU);
    UART_Remap(ENABLE, UART_TX_REMAP_PA1, UART_RX_REMAP_PA0);
  }
  PFIC_EnableIRQ(UART_IRQn);
}

uint8_t UART_IsSwapped(void) { return s_uart_swapped; }

/* MAX811S Control on PA7 */
void MAX811_Init(void) {
  /* Idle state is floating.  The external target pull-ups and MAX811S network
   * define the line; an MCU pull-up here would change the RC edge. */
  GPIOA_SetBits(MAX811_PIN);
  GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeIN_Floating);
  s_max811_seq = MAX811_SEQ_IDLE;
}

void MAX811_ResetTarget(void) {
  /* Start the sequence and return immediately.  The RF/UART main loop must
   * continue servicing packets while the target reset pulse is in progress. */
  if (s_max811_seq != MAX811_SEQ_IDLE) return;
  GPIOA_SetBits(MAX811_PIN);
  GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeOut_PP_5mA);
  s_max811_seq = MAX811_SEQ_RESET_PRECHARGE;
  max811_wait(MAX811_PRECHARGE_MS);
}

void MAX811_BootloaderTarget(void) {
  /* Establish a known high state before the trigger.  This makes repeated
   * BOOT commands produce a deliberate falling edge. */
  if (s_max811_seq != MAX811_SEQ_IDLE) return;
  GPIOA_SetBits(MAX811_PIN);
  GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeOut_PP_5mA);
  s_max811_seq = MAX811_SEQ_BOOT_PRECHARGE;
  max811_wait(MAX811_PRECHARGE_MS);
}

void MAX811_Poll(void) {
  uint32_t now;
  if (s_max811_seq == MAX811_SEQ_IDLE) return;
  now = SYS_GetSysTickCnt();
  if (!max811_due(now)) return;

  switch (s_max811_seq) {
    case MAX811_SEQ_RESET_PRECHARGE:
      GPIOA_ResetBits(MAX811_PIN);
      GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeOut_PP_5mA);
      s_max811_seq = MAX811_SEQ_RESET_LOW;
      max811_wait(MAX811_RST_TRIGGER_MS);
      break;
    case MAX811_SEQ_RESET_LOW:
      GPIOA_SetBits(MAX811_PIN);
      s_max811_seq = MAX811_SEQ_RESET_SETTLE;
      max811_wait(MAX811_RELEASE_SETTLE_MS);
      break;
    case MAX811_SEQ_RESET_SETTLE:
      GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeIN_Floating);
      s_max811_seq = MAX811_SEQ_IDLE;
      break;
    case MAX811_SEQ_BOOT_PRECHARGE:
      GPIOA_ResetBits(MAX811_PIN);
      GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeOut_PP_5mA);
      s_max811_seq = MAX811_SEQ_BOOT_LOW;
      max811_wait(MAX811_BOOT_HOLD_MS);
      break;
    case MAX811_SEQ_BOOT_LOW:
      GPIOA_SetBits(MAX811_PIN);
      s_max811_seq = MAX811_SEQ_BOOT_SETTLE;
      max811_wait(MAX811_RELEASE_SETTLE_MS);
      break;
    case MAX811_SEQ_BOOT_SETTLE:
      GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeIN_Floating);
      s_max811_seq = MAX811_SEQ_IDLE;
      break;
    default:
      s_max811_seq = MAX811_SEQ_IDLE;
      GPIOA_SetBits(MAX811_PIN);
      GPIOA_ModeCfg(MAX811_PIN, GPIO_ModeIN_Floating);
      break;
  }
}

void UART_Init(void) {
  gSysClock = GetSysClock();

  /* Disable SWD to allow PA0/PA1 to be used as full GPIO/UART */
  R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;

  MAX811_Init();

  uart_startup_rx_led();

  /* Default UART configuration */
  UART_SwapPins(0);

  UART_DefInit();
  UART_SetBuad(115200);

  UART_ByteTrigCfg(UART_4BYTE_TRIG);
  UART_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
  uart_flag = UART_STATUS_IDLE;
  uart_start_receiving();
}

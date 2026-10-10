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
#include "ina226.h"

static uint8_t uart_buf[UART_BUF_LEN];
static struct simple_buf *pUartbuf = NULL;
static struct simple_buf uart_buffer;

volatile uint8_t uart_flag;
uint32_t gBaudRate;
uint32_t gSysClock;
uint32_t gIntervalTimer;
uint32_t gUartRxCount;

static uint8_t s_uart_swapped = 0;

#if PROBE_WITHOUT_UART
/* Probe PA0/PA1 are active-low LEDs in the temporary --without-UART build. */
#define PROBE_LED_CURRENT_PIN       TXD_PIN_PA0
#define PROBE_LED_VOLTAGE_PIN       RXD_PIN_PA1
#define PROBE_LED_PINS              (PROBE_LED_CURRENT_PIN | PROBE_LED_VOLTAGE_PIN)
#define PROBE_LED_PWM_HZ             250u
#define PROBE_LED_PWM_STEPS          64u
#define PROBE_LED_PWM_ISR_HZ         (PROBE_LED_PWM_HZ * PROBE_LED_PWM_STEPS)
#define PROBE_LED_STARTUP_MS         3000u
#define PROBE_LED_CURRENT_TAU_MS     120u
#define PROBE_LED_DUTY_MAX           4095u

extern dev_config_t g_dev_config;
static volatile uint16_t s_probe_led_voltage_duty = 0;
static volatile uint16_t s_probe_led_current_duty = 0;
static uint8_t s_probe_led_phase = 0;
static uint8_t s_probe_led_initialized = 0;
static uint32_t s_probe_led_last_poll = 0;
static uint32_t s_probe_led_phase_tick = 0;
static uint32_t s_probe_led_phase_q16 = 0;
static uint32_t s_probe_led_period_ms = 4000u;
static uint16_t s_probe_led_current_smooth = 0;

static const uint8_t s_probe_led_breath_lut[33] = {
  0, 1, 3, 6, 10, 16, 24, 34, 46, 60, 76, 94, 113, 133, 153, 173,
  192, 209, 224, 237, 247, 253, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255
};

static uint8_t probe_led_breath_value(uint32_t phase_q16)
{
  uint32_t pos = (phase_q16 * 64u) >> 16;
  if (pos > 64u) pos = 64u;
  if (pos <= 32u) return s_probe_led_breath_lut[pos];
  return s_probe_led_breath_lut[64u - pos];
}

static void probe_led_voltage_profile(uint32_t bus_mv, uint32_t *period_ms,
                                      uint16_t *max_duty)
{
  /* Nearest nominal rail: 5 V (<=7), 9 V (<=10.5), 12 V (<=15), 18 V. */
  if (bus_mv <= 7000u) {
    *period_ms = 4000u; *max_duty = 1638u; /* 40% */
  } else if (bus_mv <= 10500u) {
    *period_ms = 3000u; *max_duty = 2457u; /* 60% */
  } else if (bus_mv <= 15000u) {
    *period_ms = 2000u; *max_duty = 3276u; /* 80% */
  } else {
    *period_ms = 1000u; *max_duty = 4095u; /* 18 V rail */
  }
}

/* Called from the 16 kHz timer ISR.  The 64-step carrier and fractional
 * accumulator give a smooth active-low output while keeping ISR work tiny. */
__HIGH_CODE
void Probe_LED_TimerISR(void)
{
  static uint8_t level_current = 0, level_voltage = 0;
  static uint8_t frac_current = 0, frac_voltage = 0;
  uint16_t d_current = s_probe_led_current_duty;
  uint16_t d_voltage = s_probe_led_voltage_duty;
  uint8_t base_current = (uint8_t)(d_current >> 6);
  uint8_t base_voltage = (uint8_t)(d_voltage >> 6);
  uint32_t on = 0;

  frac_current = (uint8_t)(frac_current + (d_current & 0x3Fu));
  frac_voltage = (uint8_t)(frac_voltage + (d_voltage & 0x3Fu));
  level_current = (uint8_t)(base_current + (frac_current >= 64u));
  level_voltage = (uint8_t)(base_voltage + (frac_voltage >= 64u));
  if (frac_current >= 64u) frac_current = (uint8_t)(frac_current - 64u);
  if (frac_voltage >= 64u) frac_voltage = (uint8_t)(frac_voltage - 64u);
  if (level_current > PROBE_LED_PWM_STEPS) level_current = PROBE_LED_PWM_STEPS;
  if (level_voltage > PROBE_LED_PWM_STEPS) level_voltage = PROBE_LED_PWM_STEPS;

  s_probe_led_phase = (uint8_t)((s_probe_led_phase + 1u) & (PROBE_LED_PWM_STEPS - 1u));
  if (s_probe_led_phase < level_current) on |= PROBE_LED_CURRENT_PIN;
  if (s_probe_led_phase < level_voltage) on |= PROBE_LED_VOLTAGE_PIN;
  GPIOA_ResetBits(on);
  GPIOA_SetBits(PROBE_LED_PINS & ~on);
}

void Probe_LED_Init(void)
{
  /* PA0/PA1 share the debug alternate function on CH57x.  UART_Init used to
   * clear this bit; the --without-UART path must do it explicitly or both
   * LED GPIO writes are ignored. */
  R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
  GPIOA_ModeCfg(PROBE_LED_PINS, GPIO_ModeOut_PP_5mA);
  GPIOA_SetBits(PROBE_LED_PINS);
  /* Preserve the existing power-on indication: PA1 is lit for three seconds. */
  GPIOA_ResetBits(PROBE_LED_VOLTAGE_PIN);
  mDelaymS(PROBE_LED_STARTUP_MS);
  GPIOA_SetBits(PROBE_LED_PINS);

  s_probe_led_voltage_duty = 1638u;
  s_probe_led_current_duty = 0;
  s_probe_led_current_smooth = 0;
  s_probe_led_phase = 0;
  s_probe_led_phase_q16 = 0;
  s_probe_led_last_poll = SYS_GetSysTickCnt();
  s_probe_led_phase_tick = s_probe_led_last_poll;
  s_probe_led_initialized = 1;

  R32_TMR_CNT_END = GetSysClock() / PROBE_LED_PWM_ISR_HZ;
  R8_TMR_CTRL_MOD = RB_TMR_ALL_CLEAR;
  R8_TMR_CTRL_DMA = 0;
  R8_TMR_INT_FLAG = RB_TMR_IF_CYC_END;
  R8_TMR_INTER_EN = RB_TMR_IE_CYC_END;
  R8_TMR_CTRL_MOD = RB_TMR_COUNT_EN;
  PFIC_EnableIRQ(TMR_IRQn);
}

void Probe_LED_Poll(void)
{
  uint32_t now, ticks_per_ms, elapsed_ms, dt_ms;
  ina226_data_t data;
  uint32_t period_ms;
  uint16_t max_duty;
  uint8_t breath;
  int32_t current_ua;
  uint32_t fullscale_ua;
  uint32_t target_duty;
  uint32_t alpha_q16;

  if (!s_probe_led_initialized) return;
  now = SYS_GetSysTickCnt();
  ticks_per_ms = GetSysClock() / 1000u;
  if (ticks_per_ms == 0u) ticks_per_ms = 1u;
  elapsed_ms = (uint32_t)((now - s_probe_led_phase_tick) / ticks_per_ms);
  if (elapsed_ms != 0u) {
    s_probe_led_phase_tick += elapsed_ms * ticks_per_ms;
    s_probe_led_phase_q16 = (s_probe_led_phase_q16 +
      (uint32_t)(((uint64_t)elapsed_ms << 16) / s_probe_led_period_ms)) & 0xFFFFu;
  }

  if ((uint32_t)((now - s_probe_led_last_poll) / ticks_per_ms) < 50u) return;
  dt_ms = (uint32_t)((now - s_probe_led_last_poll) / ticks_per_ms);
  s_probe_led_last_poll = now;
  if (dt_ms > 500u) dt_ms = 500u;

  if (!INA226_ReadData(&data)) return;
  probe_led_voltage_profile(data.bus_mv, &period_ms, &max_duty);
  s_probe_led_period_ms = period_ms;
  {
    uint16_t configured_max = g_dev_config.link_led_max_duty;
    if (configured_max == 0u) configured_max = 255u;
    if (configured_max < 26u) configured_max = 26u;
    max_duty = (uint16_t)(((uint32_t)max_duty * configured_max + 127u) / 255u);
  }
  breath = probe_led_breath_value(s_probe_led_phase_q16);
  s_probe_led_voltage_duty = (uint16_t)(((uint32_t)max_duty * breath + 127u) / 255u);

  current_ua = data.current_ua;
  if (current_ua < 0) current_ua = -current_ua;
  fullscale_ua = (g_dev_config.full_scale_ma > 0u) ?
                 ((uint32_t)g_dev_config.full_scale_ma * 1000u) : 1000000u;
  target_duty = ((uint32_t)current_ua >= fullscale_ua) ?
                PROBE_LED_DUTY_MAX : ((uint32_t)current_ua * PROBE_LED_DUTY_MAX) / fullscale_ua;
  if (current_ua > 0 && target_duty < 120u) target_duty = 120u;
  alpha_q16 = (dt_ms * 65535u) / (PROBE_LED_CURRENT_TAU_MS + dt_ms);
  if (target_duty > s_probe_led_current_smooth)
    s_probe_led_current_smooth += (uint16_t)(((target_duty - s_probe_led_current_smooth) * alpha_q16) >> 16);
  else
    s_probe_led_current_smooth -= (uint16_t)(((s_probe_led_current_smooth - target_duty) * alpha_q16) >> 16);
  s_probe_led_current_duty = s_probe_led_current_smooth;
}
#endif

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
#if PROBE_WITHOUT_UART
    Probe_LED_TimerISR();
#else
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
#endif
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

/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_uart.c
 * Author             : WCH / Modified for Wireless Current Sensor Tool
 * Version            : V2.0
 * Date               : 2026/09/23
 * Description        : Dual CDC-ACM (COM1 MCU Passthrough, COM2 Telemetry & Control)
 *******************************************************************************/

#include "usb_uart.h"
#include "rf_uart_rx.h"
#include "CH57x_pwm.h"
#include <stdio.h>
#include <string.h>

#define LED1_FAST_BYTES_5S 1024u
#define LED2_FILTER_TAU_MS 700u
#include <stdlib.h>

#define THIS_ENDP0_SIZE         64
#define MAX_PACKET_SIZE         64
#define EP0_DATA1               (RB_UEP_R_TOG | RB_UEP_T_TOG)
#define LINK_LED_DEFAULT_DUTY  255u
#define LED_SOFTWARE_PWM_FALLBACK 1
#define LED_SOFTWARE_PWM_HZ       250u
#define LED_SOFTWARE_PWM_STEPS    64u
#define LED_SOFTWARE_PWM_ISR_HZ   (LED_SOFTWARE_PWM_HZ * LED_SOFTWARE_PWM_STEPS)

/* ---------------- USB Descriptors ---------------- */

/* Device Descriptor (IAD Composite) */
const uint8_t TAB_USB_CDC_DEV_DES[18] =
{
    0x12,       // bLength
    0x01,       // bDescriptorType (Device)
    0x00, 0x02, // bcdUSB 2.00
    0xEF,       // bDeviceClass (Miscellaneous Device)
    0x02,       // bDeviceSubClass (Common Class)
    0x01,       // bDeviceProtocol (IAD)
    0x40,       // bMaxPacketSize0 = 64
    0x86, 0x1A, // idVendor (0x1A86 - WCH)
    0x0C, 0xFE, // idProduct (0xFE0C - Non-CH340 Dual CDC PID)
    0x00, 0x01, // bcdDevice 1.00
    0x01,       // iManufacturer = 1
    0x02,       // iProduct = 2
    0x03,       // iSerialNumber = 3
    0x01        // bNumConfigurations = 1
};

/* Configuration Descriptor (Dual CDC: 4 Interfaces, 2 IADs, Total Length = 141) */
const uint8_t TAB_USB_CDC_CFG_DES[141] =
{
    /* Configuration Descriptor (9 bytes) */
    0x09, 0x02, 0x8D, 0x00, 0x04, 0x01, 0x00, 0x80, 0x32,

    /* ---------------- IAD 0 (COM1 - MCU Passthrough) ---------------- */
    0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x01, 0x04,   /* iFunction = "CH570 Wireless UART" */

    /* Interface 0: CDC Communication Interface (COM1 Control) */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x04,
    /* Header Functional Descriptor */
    0x05, 0x24, 0x00, 0x10, 0x01,
    /* Call Management Functional Descriptor */
    0x05, 0x24, 0x01, 0x00, 0x01,
    /* Abstract Control Management (ACM) Descriptor */
    0x04, 0x24, 0x02, 0x02,
    /* Union Functional Descriptor */
    0x05, 0x24, 0x06, 0x00, 0x01,
    /* Endpoint 4 IN (Interrupt Notification for COM1) */
    0x07, 0x05, 0x84, 0x03, 0x08, 0x00, 0x10,

    /* Interface 1: CDC Data Interface (COM1 Data) */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    /* Endpoint 1 OUT (Bulk Data PC -> MCU) */
    0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,
    /* Endpoint 1 IN (Bulk Data MCU -> PC) */
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,

    /* ---------------- IAD 1 (COM2 - Telemetry & Control) ---------------- */
    0x08, 0x0B, 0x02, 0x02, 0x02, 0x02, 0x01, 0x05,   /* iFunction = "CH570 Sensor Control" */

    /* Interface 2: CDC Communication Interface (COM2 Control) */
    0x09, 0x04, 0x02, 0x00, 0x01, 0x02, 0x02, 0x01, 0x05,
    /* Header Functional Descriptor */
    0x05, 0x24, 0x00, 0x10, 0x01,
    /* Call Management Functional Descriptor */
    0x05, 0x24, 0x01, 0x00, 0x03,
    /* Abstract Control Management (ACM) Descriptor */
    0x04, 0x24, 0x02, 0x02,
    /* Union Functional Descriptor */
    0x05, 0x24, 0x06, 0x02, 0x03,
    /* Endpoint 3 IN (Interrupt Notification for COM2) */
    0x07, 0x05, 0x83, 0x03, 0x08, 0x00, 0x10,

    /* Interface 3: CDC Data Interface (COM2 Data) */
    0x09, 0x04, 0x03, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    /* Endpoint 2 OUT (Bulk Data PC -> Command Parser) */
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
    /* Endpoint 2 IN (Bulk Data Telemetry -> PC) */
    0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00
};

/* String Descriptors (UTF-16LE, bLength = 2 + 2*char_count) */
const uint8_t TAB_USB_LID_STR_DES[] = { 0x04, 0x03, 0x09, 0x04 }; // 0x0409 English
const uint8_t TAB_USB_VEN_STR_DES[] = { 0x1A, 0x03, 'W',0,'C',0,'H',0,' ',0,'W',0,'i',0,'r',0,'e',0,'l',0,'e',0,'s',0,'s',0 };
const uint8_t TAB_USB_PRD_STR_DES[] = { 0x26, 0x03, 'C',0,'H',0,'5',0,'7',0,'0',0,' ',0,'W',0,'i',0,'r',0,'e',0,'l',0,'e',0,'s',0,'s',0,' ',0,'C',0,'O',0,'M',0 };
const uint8_t TAB_USB_SER_STR_DES[] = { 0x14, 0x03, 'C',0,'H',0,'5',0,'7',0,'0',0,'-',0,'V',0,'2',0,'0',0 };
/* Per-function names referenced by each IAD's iFunction / interface iInterface,
 * so the two virtual COM ports can be told apart. */
const uint8_t TAB_USB_COM1_STR_DES[] = { 0x28, 0x03, 'C',0,'H',0,'5',0,'7',0,'0',0,' ',0,'W',0,'i',0,'r',0,'e',0,'l',0,'e',0,'s',0,'s',0,' ',0,'U',0,'A',0,'R',0,'T',0 };
const uint8_t TAB_USB_COM2_STR_DES[] = { 0x2A, 0x03, 'C',0,'H',0,'5',0,'7',0,'0',0,' ',0,'S',0,'e',0,'n',0,'s',0,'o',0,'r',0,' ',0,'C',0,'o',0,'n',0,'t',0,'r',0,'o',0,'l',0 };

/* CDC Line Coding defaults */
static LINE_CODE s_com1_line_coding = { 115200, 0, 0, 8, 0 };
static LINE_CODE s_com2_line_coding = { 115200, 0, 0, 8, 0 };

LINE_CODE Uart0Para = { 115200, 0, 0, 8, 0 };
uint8_t   UART_Status = 0;

/* Hardware Endpoints DMA Buffers */
__aligned(4) uint8_t Ep0Buffer[64 + 64 + 64];   // EP0 (64) + EP4 OUT (64) + EP4 IN (64)
__aligned(4) uint8_t Ep1Buffer[64 + 64];        // EP1 OUT (64) + EP1 IN (64)
__aligned(4) uint8_t Ep2Buffer[64 + 64];        // EP2 OUT (64) + EP2 IN (64)
__aligned(4) uint8_t Ep3Buffer[64 + 64];        // EP3 OUT (64) + EP3 IN (64)

/* USB Setup Request structure */
typedef struct __PACKED _USB_SETUP_REQ_ {
    uint8_t  bRequestType;
    uint8_t  bRequest;
    uint8_t  wValueL;
    uint8_t  wValueH;
    uint8_t  wIndexL;
    uint8_t  wIndexH;
    uint8_t  wLengthL;
    uint8_t  wLengthH;
} USB_SETUP_REQ_t;

#define UsbSetupBuf     ((USB_SETUP_REQ_t *)Ep0Buffer)

static uint8_t s_usb_address = 0;
static uint8_t s_usb_config  = 0;
static uint8_t s_setup_req   = 0;
static uint8_t s_setup_iface = 0;
static uint16_t s_setup_len  = 0;
static const uint8_t *s_p_descr = NULL;

/* In-flight IN flags */
static volatile uint8_t s_ep1_in_busy = 0;
static volatile uint8_t s_ep2_in_busy = 0;

/* ---------------- FIFO Buffers ---------------- */

/* COM1 FIFO: PC EP1 OUT -> MCU RF */
#define COM1_RX_FIFO_SIZE 640
static uint8_t  s_com1_rx_fifo[COM1_RX_FIFO_SIZE] __attribute__((aligned(4)));
static uint16_t s_com1_rx_head = 0;
static uint16_t s_com1_rx_tail = 0;
static volatile uint16_t s_com1_rx_count = 0;

/* COM1 FIFO: MCU RF -> PC EP1 IN */
#define COM1_TX_FIFO_SIZE 384
static uint8_t  s_com1_tx_fifo[COM1_TX_FIFO_SIZE];
static uint16_t s_com1_tx_head = 0;
static uint16_t s_com1_tx_tail = 0;
static volatile uint16_t s_com1_tx_count = 0;

/* COM2 FIFO: Telemetry / Response -> PC EP2 IN */
#define COM2_TX_FIFO_SIZE 192
static uint8_t  s_com2_tx_fifo[COM2_TX_FIFO_SIZE];
static uint16_t s_com2_tx_head = 0;
static uint16_t s_com2_tx_tail = 0;
static volatile uint16_t s_com2_tx_count = 0;

/* COM2 Command line buffer (PC EP2 OUT) */
#define COM2_CMD_BUF_SIZE 160
static char    s_com2_cmd_buf[COM2_CMD_BUF_SIZE];
static uint16_t s_com2_cmd_len = 0;
static volatile uint8_t s_com2_cmd_ready = 0;

/* Pending control command to Probe over RF */
static ctrl_cmd_pkt_t s_pending_cmd = { 0, 0, 0, 0 };
static volatile uint8_t s_has_pending_cmd = 0;
static uint8_t s_dtr_rts_state = 0;
static uint8_t s_dtr_rts_initialized = 0;
/* CDC wValue uses asserted DTR/RTS bits, independent of the active-low
 * voltage on CH340 pins.  Keep reset events separate from COM2 commands. */
#define RESET_EVENT_QUEUE_SIZE 2u
#define RESET_SEQUENCE_TIMEOUT_MS 500u
#define RESET_CANDIDATE_NONE 0u
#define RESET_CANDIDATE_CLASSIC 1u
#define RESET_CANDIDATE_ATOMIC 2u
static uint8_t s_reset_events[RESET_EVENT_QUEUE_SIZE];
static volatile uint8_t s_reset_event_head = 0;
static volatile uint8_t s_reset_event_count = 0;
static uint8_t s_dtr_rts_candidate = RESET_CANDIDATE_NONE;
static uint32_t s_dtr_rts_candidate_tick = 0;
static uint8_t s_dtr_rts_boot_active = 0;
static uint32_t s_dtr_rts_boot_tick = 0;

/* Telemetry & LED status */
volatile uint16_t g_current_duty = 0;
volatile uint32_t g_current_report_tick = 0;
volatile uint32_t g_com1_activity_bytes = 0;
static volatile uint8_t s_led_test_mode = 0;
static volatile uint8_t s_led_sw_phase = 0;
static volatile uint16_t s_led_sw_duty1 = 0; /* 12-bit q0..4095 */
static volatile uint16_t s_led_sw_duty2 = 0;
dev_config_t g_dongle_cfg = { CFG_MAGIC, 20000, 64, 100, 20000, 0, LINK_LED_DEFAULT_DUTY, 4 };

/* Forward Declarations */
static void COM1_CheckTxToHost(void);
static void COM2_CheckTxToHost(void);
static void COM2_ProcessCommand(char *cmd);
static void LED_SoftwarePwmOutput(uint32_t now);
static void LED_TestApply(void);
static void LED_PWM_UpdateDuty(void);

/* ---------------- COM1 API ---------------- */

static void COM1_RxPush(uint8_t b)
{
    if (USB_FirmwareUpdateActive()) return;
    if (s_com1_rx_count < COM1_RX_FIFO_SIZE)
    {
        s_com1_rx_fifo[s_com1_rx_head] = b;
        s_com1_rx_head = (s_com1_rx_head + 1) % COM1_RX_FIFO_SIZE;
        s_com1_rx_count++;
    }
}

uint8_t COM1_RxPop(uint8_t *byte)
{
    uint32_t irqv;
    SYS_DisableAllIrq(&irqv);
    if (s_com1_rx_count > 0)
    {
        *byte = s_com1_rx_fifo[s_com1_rx_tail];
        s_com1_rx_tail = (s_com1_rx_tail + 1) % COM1_RX_FIFO_SIZE;
        s_com1_rx_count--;
        if ((COM1_RX_FIFO_SIZE - s_com1_rx_count) >= 256)
        {
            if ((R8_UEP1_CTRL & MASK_UEP_R_RES) == UEP_R_RES_NAK)
            {
                R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK;
            }
        }
        SYS_RecoverIrq(irqv);
        return 1;
    }
    SYS_RecoverIrq(irqv);
    return 0;
}

uint8_t USB_RxQuery(void *buf, typeBufSize *len)
{
    uint8_t *p = (uint8_t *)buf;
    typeBufSize max_len = *len;
    typeBufSize cnt = 0;

    if (USB_FirmwareUpdateActive())
    {
        *len = 0;
        return 0xFF;
    }

    if (UART_Status)
    {
        UART_Status = 0;
        *len = 0;
        return 0x80; // Baud rate changed
    }

    if (s_com1_rx_count > 0)
    {
        while (cnt < max_len && s_com1_rx_count > 0)
        {
            COM1_RxPop(&p[cnt++]);
        }
        *len = cnt;
        return 0x00; // Normal data
    }

    *len = 0;
    return 0xFF; // No data
}

void COM1_SendBytes(const uint8_t *data, uint16_t len)
{
    uint32_t irqv;
    SYS_DisableAllIrq(&irqv);
    /* Atomic check: only enqueue if the full block fits */
    if ((COM1_TX_FIFO_SIZE - s_com1_tx_count) >= len)
    {
        for (uint16_t i = 0; i < len; i++)
        {
            s_com1_tx_fifo[s_com1_tx_head] = data[i];
            s_com1_tx_head = (s_com1_tx_head + 1) % COM1_TX_FIFO_SIZE;
        }
        s_com1_tx_count += len;
    }
    COM1_CheckTxToHost();
    SYS_RecoverIrq(irqv);
}

static void COM1_CheckTxToHost(void)
{
    if (USB_FirmwareUpdateActive()) return;
    if (!s_ep1_in_busy && s_usb_address)
    {
        if (s_com1_tx_count > 0)
        {
            uint8_t send_len = (s_com1_tx_count > 64) ? 64 : (uint8_t)s_com1_tx_count;
            for (uint8_t i = 0; i < send_len; i++)
            {
                Ep1Buffer[64 + i] = s_com1_tx_fifo[s_com1_tx_tail];
                s_com1_tx_tail = (s_com1_tx_tail + 1) % COM1_TX_FIFO_SIZE;
            }
            s_com1_tx_count -= send_len;
            s_ep1_in_busy = 1;
            R8_UEP1_T_LEN = send_len;
            R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
        }
        else
        {
            R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
        }
    }
}

/* ---------------- COM2 API ---------------- */

void COM2_SendBytes(const char *data, uint16_t len)
{
    uint32_t irqv;
    SYS_DisableAllIrq(&irqv);
    /* Atomic check: only enqueue if the entire line fits; never shred lines */
    if ((COM2_TX_FIFO_SIZE - s_com2_tx_count) >= len)
    {
        for (uint16_t i = 0; i < len; i++)
        {
            s_com2_tx_fifo[s_com2_tx_head] = (uint8_t)data[i];
            s_com2_tx_head = (s_com2_tx_head + 1) % COM2_TX_FIFO_SIZE;
        }
        s_com2_tx_count += len;
    }
    COM2_CheckTxToHost();
    SYS_RecoverIrq(irqv);
}

/* Keep the frequent self-update ACK out of printf/sprintf.  The libc
 * formatter has a large transient stack frame on CH570 and can overlap the
 * RF callback stack while a block is being written. */
static void COM2_SendFwAck(uint32_t value)
{
    char rsp[16];
    uint8_t digits[10];
    uint8_t n = 0;
    uint8_t i;
    if (value == 0) digits[n++] = 0;
    while (value != 0 && n < sizeof(digits))
    {
        digits[n++] = (uint8_t)(value % 10u);
        value /= 10u;
    }
    rsp[0] = '['; rsp[1] = 'O'; rsp[2] = 'K'; rsp[3] = ']'; rsp[4] = ' ';
    for (i = 0; i < n; i++) rsp[5 + i] = (char)('0' + digits[n - i - 1]);
    rsp[5 + n] = '\r'; rsp[6 + n] = '\n';
    COM2_SendBytes(rsp, (uint16_t)(7 + n));
}

static void COM2_CheckTxToHost(void)
{
    if (!s_ep2_in_busy && s_usb_address)
    {
        if (s_com2_tx_count > 0)
        {
            uint8_t send_len = (s_com2_tx_count > 64) ? 64 : (uint8_t)s_com2_tx_count;
            for (uint8_t i = 0; i < send_len; i++)
            {
                Ep2Buffer[64 + i] = s_com2_tx_fifo[s_com2_tx_tail];
                s_com2_tx_tail = (s_com2_tx_tail + 1) % COM2_TX_FIFO_SIZE;
            }
            s_com2_tx_count -= send_len;
            s_ep2_in_busy = 1;
            R8_UEP2_T_LEN = send_len;
            R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
        }
        else
        {
            R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
        }
    }
}

static void COM1_QueueResetEvent(uint8_t opcode)
{
    uint8_t tail;
    if (s_reset_event_count == RESET_EVENT_QUEUE_SIZE)
    {
        /* Preserve the newest request when RF has been unavailable. */
        s_reset_event_head = (uint8_t)((s_reset_event_head + 1u) % RESET_EVENT_QUEUE_SIZE);
        s_reset_event_count--;
    }
    tail = (uint8_t)((s_reset_event_head + s_reset_event_count) % RESET_EVENT_QUEUE_SIZE);
    s_reset_events[tail] = opcode;
    s_reset_event_count++;
}

static void COM1_HandleControlLines(uint8_t next_state)
{
    uint8_t prev_state = s_dtr_rts_state;
    uint32_t now = SYS_GetSysTickCnt();
    uint32_t timeout_ticks = (GetSysClock() / 1000u) * RESET_SEQUENCE_TIMEOUT_MS;

    if (s_dtr_rts_candidate != RESET_CANDIDATE_NONE &&
        (uint32_t)(now - s_dtr_rts_candidate_tick) > timeout_ticks)
        s_dtr_rts_candidate = RESET_CANDIDATE_NONE;
    if (s_dtr_rts_boot_active &&
        (uint32_t)(now - s_dtr_rts_boot_tick) > timeout_ticks)
        s_dtr_rts_boot_active = 0;

    s_dtr_rts_state = next_state;
    if (!s_dtr_rts_initialized)
    {
        s_dtr_rts_initialized = 1;
        if (next_state == 2u)
        {
            s_dtr_rts_candidate = RESET_CANDIDATE_CLASSIC;
            s_dtr_rts_candidate_tick = now;
        }
        return;
    }
    if (prev_state == next_state) return;

    if (prev_state == 2u && s_dtr_rts_candidate != RESET_CANDIDATE_NONE)
    {
        if (next_state == 3u ||
            (next_state == 1u && s_dtr_rts_candidate == RESET_CANDIDATE_ATOMIC))
        {
            COM1_QueueResetEvent(CTRL_OP_BOOT);
            s_dtr_rts_boot_active = 1;
            s_dtr_rts_boot_tick = now;
        }
        else if (next_state == 0u)
        {
            COM1_QueueResetEvent(CTRL_OP_RESET);
        }
        s_dtr_rts_candidate = RESET_CANDIDATE_NONE;
    }
    else if (prev_state == 3u && next_state == 1u && !s_dtr_rts_boot_active)
    {
        /* A hard reset can start from the driver's asserted-DTR state. */
        COM1_QueueResetEvent(CTRL_OP_RESET);
    }

    if (next_state == 2u)
    {
        /* 3->2->1 is the POSIX atomic sequence; 2->3 is the Windows
         * classic sequence, including when the port opened in state 3. */
        s_dtr_rts_candidate = (prev_state == 3u) ?
            RESET_CANDIDATE_ATOMIC : RESET_CANDIDATE_CLASSIC;
        s_dtr_rts_candidate_tick = now;
        s_dtr_rts_boot_active = 0;
    }
    else if (next_state == 0u)
    {
        s_dtr_rts_candidate = RESET_CANDIDATE_NONE;
        s_dtr_rts_boot_active = 0;
    }
    else if (prev_state == 1u && next_state == 3u)
    {
        s_dtr_rts_boot_active = 0;
    }
}

uint8_t COM2_PopPendingCmd(void *cmd)
{
    uint32_t irqv;
    uint8_t found = 0;
    SYS_DisableAllIrq(&irqv);
    if (s_reset_event_count)
    {
        ctrl_cmd_pkt_t event_cmd = { 0, 0, 0, 0, 0, 0, 0 };
        event_cmd.opcode = s_reset_events[s_reset_event_head];
        memcpy(cmd, &event_cmd, sizeof(event_cmd));
        s_reset_event_head = (uint8_t)((s_reset_event_head + 1u) % RESET_EVENT_QUEUE_SIZE);
        s_reset_event_count--;
        found = 1;
    }
    else if (s_has_pending_cmd)
    {
        memcpy(cmd, &s_pending_cmd, sizeof(s_pending_cmd));
        s_has_pending_cmd = 0;
        s_pending_cmd.opcode = 0;
        found = 1;
    }
    SYS_RecoverIrq(irqv);
    return found;
}

/* Parse simple integer from string */
static int32_t parse_int(const char *s)
{
    int32_t res = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9')
    {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}

/* Enter the factory ISP bootloader by wiping the user reset vector.
 *
 * CH57x_gen2 (CH570Q / CH572D-Q-E ...) boot ROM behaviour: when flash address
 * 0 is in the erased state (0xFFFFFFFF) there is no valid application to jump
 * to, so after a reset the chip stays resident in the factory bootloader
 * (USB ISP on PA0=D-/PA1=D+, or UART ISP on PA0=RX/PA1=TX) and WCHISPTool can
 * re-flash it. Runs entirely from RAM (.highcode) with interrupts disabled,
 * because once sector 0 is erased the vector table and any code resident there
 * are gone and must not be fetched again. */
__HIGH_CODE
static uint8_t Dongle_EnterDFU(void)
{
    uint32_t irqv;

    SYS_DisableAllIrq(&irqv);            /* ISRs' vectors live in sector 0 */
    if (FLASH_ROM_ERASE(0x00000000, 4096) == 0)
    {
        SYS_ResetExecute();              /* software reset -> ROM stays in ISP (blank flash) */
        while (1) { }                    /* never reached */
    }
    SYS_RecoverIrq(irqv);
    return 0;
}

#define DONGLE_SLOT_B_ADDR    0x00020000u
#define DONGLE_MAX_FW_SIZE    32768u

static uint32_t s_fw_total_size = 0;
static uint32_t s_fw_expected_crc = 0;
static uint8_t  s_fw_in_progress = 0;
static uint8_t  s_fw_rf_paused = 0;

uint8_t USB_FirmwareUpdateActive(void)
{
    return s_fw_in_progress;
}

static void FW_SetRfPaused(uint8_t paused)
{
    if (paused && !s_fw_rf_paused)
    {
        PFIC_DisableIRQ(BLEB_IRQn);
        PFIC_DisableIRQ(BLEL_IRQn);
        s_fw_rf_paused = 1;
    }
    else if (!paused && s_fw_rf_paused)
    {
        PFIC_EnableIRQ(BLEB_IRQn);
        PFIC_EnableIRQ(BLEL_IRQn);
        s_fw_rf_paused = 0;
    }
}

/* Probe Wireless OTA State Variables */
volatile uint8_t s_probe_ota_pending = 0;
ota_cmd_pkt_t    s_probe_ota_cmd;
volatile uint8_t s_probe_ota_acked = 0;
ota_rsp_pkt_t    s_probe_ota_rsp;

static uint8_t   s_probe_fw_in_progress = 0;
static uint32_t  s_probe_fw_total_size = 0;
static uint32_t  s_probe_fw_expected_crc = 0;

static void COM1_CheckTxToHost(void);
static void COM2_CheckTxToHost(void);

static BOOL Dongle_WaitProbeOtaAck(uint32_t timeout_ms)
{
    uint32_t start = SYS_GetSysTickCnt();
    uint32_t timeout_ticks = timeout_ms * (GetSysClock() / 1000);

    while ((uint32_t)(SYS_GetSysTickCnt() - start) < timeout_ticks)
    {
        if (s_probe_ota_acked)
        {
            return TRUE;
        }
        uint32_t irqv;
        SYS_DisableAllIrq(&irqv);
        COM1_CheckTxToHost();
        COM2_CheckTxToHost();
        SYS_RecoverIrq(irqv);
    }
    return FALSE;
}

static uint32_t calc_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

static int hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

__HIGH_CODE
static void Dongle_ApplyFirmware(uint32_t size)
{
    uint32_t irqv;
    uint8_t failed = 0;
    uint8_t *copy_buf = (uint8_t *)s_com1_rx_fifo;
    uint32_t num_sectors = (size + 4095) / 4096;

    SYS_DisableAllIrq(&irqv);

    /* 1. Erase Slot A (sectors at 0x00000000) */
    for (uint32_t s = 0; s < num_sectors; s++)
    {
        if (FLASH_ROM_ERASE(s * 4096, 4096) != 0)
        {
            failed = 1;
            break;
        }
    }

    /* 2. Copy from Slot B (0x00020000) to Slot A (0x00000000) */
    for (uint32_t off = 0; off < size; off += 256)
    {
        if (failed) break;
        uint32_t chunk = size - off;
        if (chunk > 256) chunk = 256;

        const uint8_t *src = (const uint8_t *)(DONGLE_SLOT_B_ADDR + off);
        for (uint32_t i = 0; i < chunk; i++) copy_buf[i] = src[i];
        while (chunk % 4 != 0) copy_buf[chunk++] = 0xFF; /* 4-byte align */

        if (FLASH_ROM_WRITE(off, copy_buf, chunk) != 0)
        {
            failed = 1;
            break;
        }
    }

    if (failed)
    {
        SYS_RecoverIrq(irqv);
        Dongle_EnterDFU();
        while (1) { }
    }

    /* 3. Soft reset into new firmware at 0x00000000 */
    SYS_ResetExecute();
    while (1) { }
}


static void Dongle_Config_Load(void)
{
    dev_config_t *pCfg = (dev_config_t *)(CFG_FLASH_ADDR);
    if (pCfg->magic == CFG_MAGIC)
    {
        if (pCfg->shunt_uohm > 0) g_dongle_cfg.shunt_uohm = pCfg->shunt_uohm;
        if (pCfg->avg_samples > 0) g_dongle_cfg.avg_samples = pCfg->avg_samples;
        if (pCfg->report_interval_ms >= 10) g_dongle_cfg.report_interval_ms = pCfg->report_interval_ms;
        if (pCfg->full_scale_ma > 0) g_dongle_cfg.full_scale_ma = pCfg->full_scale_ma;
        g_dongle_cfg.uart_swapped = pCfg->uart_swapped;
        /* Keep the breathing LED visible even when an older configuration
         * contains the values 1..9.  Zero means the field was not initialized
         * by an older firmware and keeps the default full brightness. */
        g_dongle_cfg.link_led_max_duty = (pCfg->link_led_max_duty >= LINK_LED_MIN_DUTY) ?
                                         pCfg->link_led_max_duty :
                                         (pCfg->link_led_max_duty ? LINK_LED_MIN_DUTY : LINK_LED_DEFAULT_DUTY);
        if (g_dongle_cfg.avg_samples == 16) g_dongle_cfg.avg_samples = 64;
        g_dongle_cfg.ble_adv_hz = (pCfg->ble_adv_hz >= 1 && pCfg->ble_adv_hz <= 20) ? pCfg->ble_adv_hz : 4;
    }
}

static void Dongle_Config_Save(void)
{
    dev_config_t cfg;
    cfg.magic = CFG_MAGIC;
    cfg.shunt_uohm = g_dongle_cfg.shunt_uohm;
    cfg.avg_samples = g_dongle_cfg.avg_samples;
    cfg.report_interval_ms = g_dongle_cfg.report_interval_ms;
    cfg.full_scale_ma = g_dongle_cfg.full_scale_ma;
    cfg.uart_swapped = g_dongle_cfg.uart_swapped;
    cfg.link_led_max_duty = g_dongle_cfg.link_led_max_duty;
    cfg.ble_adv_hz = g_dongle_cfg.ble_adv_hz;

    /* FLASH_ROM_WRITE requires an aligned RAM buffer and a dword-sized
     * length.  The packed config is 17 bytes, so write a padded 20-byte copy. */
    uint8_t cfg_buf[(sizeof(dev_config_t) + 3u) & ~3u] __attribute__((aligned(4)));
    memset(cfg_buf, 0xFF, sizeof(cfg_buf));
    memcpy(cfg_buf, &cfg, sizeof(cfg));

    uint32_t irqv;
    SYS_DisableAllIrq(&irqv);
    FLASH_ROM_ERASE(CFG_FLASH_ADDR, 4096);
    FLASH_ROM_WRITE(CFG_FLASH_ADDR, cfg_buf, sizeof(cfg_buf));
    SYS_RecoverIrq(irqv);
}

/* COM2 ASCII Command Parser */
static void COM2_ProcessCommand(char *cmd)
{
    /* Trim trailing \r \n */
    int len = strlen(cmd);
    while (len > 0 && (cmd[len - 1] == '\r' || cmd[len - 1] == '\n' || cmd[len - 1] == ' '))
    {
        cmd[--len] = '\0';
    }
    if (len == 0) return;

    /* Every SET_CFG packet carries the complete current configuration.  This
     * keeps the new LED setting compatible with the existing command paths. */
    s_pending_cmd.param8 = (uint8_t)(g_dongle_cfg.report_interval_ms / 10);
    s_pending_cmd.param16 = g_dongle_cfg.avg_samples;
    s_pending_cmd.param32 = g_dongle_cfg.shunt_uohm;
    s_pending_cmd.full_scale_ma = g_dongle_cfg.full_scale_ma;
    s_pending_cmd.link_led_max_duty = g_dongle_cfg.link_led_max_duty;
    s_pending_cmd.ble_adv_hz = g_dongle_cfg.ble_adv_hz;

    if (strncmp(cmd, "CMD:LEDTEST=", 12) == 0)
    {
        const char *mode = cmd + 12;
        if (strcmp(mode, "NORMAL") == 0)
            s_led_test_mode = 0;
        else if (strcmp(mode, "GPIO_ON") == 0)
            s_led_test_mode = 1;
        else if (strcmp(mode, "GPIO_OFF") == 0)
            s_led_test_mode = 2;
        else if (strcmp(mode, "PWM") == 0)
            s_led_test_mode = 3;
        else if (strcmp(mode, "PWM_NO_RF") == 0)
            s_led_test_mode = 4;
        else if (strcmp(mode, "PWM8_NO_RF") == 0)
            s_led_test_mode = 5;
        else
        {
            COM2_SendBytes("[ERR] LEDTEST expects NORMAL/GPIO_ON/GPIO_OFF/PWM/PWM_NO_RF/PWM8_NO_RF\r\n",
                           sizeof("[ERR] LEDTEST expects NORMAL/GPIO_ON/GPIO_OFF/PWM/PWM_NO_RF/PWM8_NO_RF\r\n") - 1);
            return;
        }
        if (s_led_test_mode == 4 || s_led_test_mode == 5)
        {
            /* Isolate the PWM block from the RF interrupt stack. */
            PFIC_DisableIRQ(BLEB_IRQn);
            PFIC_DisableIRQ(BLEL_IRQn);
        }
        else if (s_led_test_mode == 0)
        {
            PFIC_EnableIRQ(BLEB_IRQn);
            PFIC_EnableIRQ(BLEL_IRQn);
        }
        LED_TestApply();
        COM2_SendBytes("[OK] LEDTEST set\r\n", sizeof("[OK] LEDTEST set\r\n") - 1);
    }
    else if (strcmp(cmd, "CMD:RST") == 0)
    {
        s_pending_cmd.opcode = CTRL_OP_RESET;
        s_has_pending_cmd = 1;
        COM2_SendBytes("[OK] Target Reset\r\n", sizeof("[OK] Target Reset\r\n") - 1);
    }
    else if (strcmp(cmd, "CMD:BOOT") == 0)
    {
        s_pending_cmd.opcode = CTRL_OP_BOOT;
        s_has_pending_cmd = 1;
        COM2_SendBytes("[OK] Enter Bootloader\r\n", sizeof("[OK] Enter Bootloader\r\n") - 1);
    }
    else if (strncmp(cmd, "CMD:RFTEST", 10) == 0)
    {
        int32_t duration = 1000;
        if (cmd[10] == '=') duration = parse_int(cmd + 11);
        if (!RF_bound_Flag || s_has_pending_cmd)
        {
            COM2_SendBytes("[ERR] RF link not ready\r\n", sizeof("[ERR] RF link not ready\r\n") - 1);
        }
        else if (duration < 100 || duration > 5000)
        {
            COM2_SendBytes("[ERR] RFTEST duration 100-5000ms\r\n",
                           sizeof("[ERR] RFTEST duration 100-5000ms\r\n") - 1);
        }
        else
        {
            s_pending_cmd.opcode = CTRL_OP_RF_TEST;
            s_pending_cmd.param16 = (uint16_t)duration;
            s_has_pending_cmd = 1;
            RF_TestNotifyStart();
            COM2_SendBytes("[OK] RFTEST started\r\n", sizeof("[OK] RFTEST started\r\n") - 1);
        }
    }
    else if (strcmp(cmd, "CMD:SWAP") == 0)
    {
        g_dongle_cfg.uart_swapped = !g_dongle_cfg.uart_swapped;
        s_pending_cmd.opcode = CTRL_OP_SWAP_UART;
        s_pending_cmd.param8 = g_dongle_cfg.uart_swapped;
        s_has_pending_cmd = 1;
        Dongle_Config_Save();
        COM2_SendBytes("[OK] Target UART Swapped\r\n", sizeof("[OK] Target UART Swapped\r\n") - 1);
    }
    else if (strncmp(cmd, "CMD:FSC=", 8) == 0)
    {
        int32_t val = parse_int(cmd + 8);
        if (val > 0 && val <= 20000)
        {
            g_dongle_cfg.full_scale_ma = (uint16_t)val;
            s_pending_cmd.opcode = CTRL_OP_SET_CFG;
            s_pending_cmd.param8 = (uint8_t)(g_dongle_cfg.report_interval_ms / 10);
            s_pending_cmd.param16 = g_dongle_cfg.avg_samples;
            s_pending_cmd.param32 = g_dongle_cfg.shunt_uohm;
            s_pending_cmd.full_scale_ma = g_dongle_cfg.full_scale_ma;
            s_pending_cmd.full_scale_ma = g_dongle_cfg.full_scale_ma;
            s_has_pending_cmd = 1;
            Dongle_Config_Save();
            COM2_SendBytes("[OK] FSC Set\r\n", sizeof("[OK] FSC Set\r\n") - 1);
        }
        else
        {
            COM2_SendBytes("[ERR] Invalid FSC (1-20000mA)\r\n",
                           sizeof("[ERR] Invalid FSC (1-20000mA)\r\n") - 1);
        }
    }
    else if (strncmp(cmd, "CMD:LINKLED=", 12) == 0)
    {
        int32_t val = parse_int(cmd + 12);
        if (val >= LINK_LED_MIN_DUTY && val <= 255)
        {
            g_dongle_cfg.link_led_max_duty = (uint8_t)val;
            s_pending_cmd.opcode = CTRL_OP_SET_CFG;
            s_pending_cmd.link_led_max_duty = g_dongle_cfg.link_led_max_duty;
            s_has_pending_cmd = 1;
            Dongle_Config_Save();
            COM2_SendBytes("[OK] Link LED Set\r\n", sizeof("[OK] Link LED Set\r\n") - 1);
        }
        else
        {
            COM2_SendBytes("[ERR] Invalid Link LED (10%-100%)\r\n",
                           sizeof("[ERR] Invalid Link LED (10%-100%)\r\n") - 1);
        }
    }
    else if (strncmp(cmd, "CMD:BLE=", 8) == 0)
    {
        int32_t val = parse_int(cmd + 8);
        if (val >= 1 && val <= 20)
        {
            g_dongle_cfg.ble_adv_hz = (uint8_t)val;
            s_pending_cmd.opcode = CTRL_OP_SET_CFG;
            s_pending_cmd.ble_adv_hz = g_dongle_cfg.ble_adv_hz;
            s_has_pending_cmd = 1;
            Dongle_Config_Save();
            COM2_SendBytes("[OK] BLE Rate Set\r\n", sizeof("[OK] BLE Rate Set\r\n") - 1);
        }
        else
        {
            COM2_SendBytes("[ERR] Invalid BLE rate (1-20Hz)\r\n",
                           sizeof("[ERR] Invalid BLE rate (1-20Hz)\r\n") - 1);
        }
    }
    else if (strncmp(cmd, "CMD:RATE=", 9) == 0)
    {
        int32_t val = parse_int(cmd + 9);
        if (val >= 10 && val <= 1000)
        {
            g_dongle_cfg.report_interval_ms = (uint16_t)val;
            s_pending_cmd.opcode = CTRL_OP_SET_CFG;
            s_pending_cmd.param8 = (uint8_t)(val / 10);
            s_pending_cmd.param16 = g_dongle_cfg.avg_samples;
            s_pending_cmd.param32 = g_dongle_cfg.shunt_uohm;
            s_pending_cmd.full_scale_ma = g_dongle_cfg.full_scale_ma;
            s_has_pending_cmd = 1;
            Dongle_Config_Save();
            COM2_SendBytes("[OK] Rate Set\r\n", sizeof("[OK] Rate Set\r\n") - 1);
        }
        else
        {
            COM2_SendBytes("[ERR] Invalid Rate (10-1000ms)\r\n", sizeof("[ERR] Invalid Rate (10-1000ms)\r\n") - 1);
        }
    }
    else if (strncmp(cmd, "CMD:SHUNT=", 10) == 0)
    {
        int32_t val = parse_int(cmd + 10);
        if (val > 0)
        {
            g_dongle_cfg.shunt_uohm = (uint32_t)val;
            s_pending_cmd.opcode = CTRL_OP_SET_CFG;
            s_pending_cmd.param8 = (uint8_t)(g_dongle_cfg.report_interval_ms / 10);
            s_pending_cmd.param16 = g_dongle_cfg.avg_samples;
            s_pending_cmd.param32 = g_dongle_cfg.shunt_uohm;
            s_pending_cmd.full_scale_ma = g_dongle_cfg.full_scale_ma;
            s_has_pending_cmd = 1;
            Dongle_Config_Save();
            COM2_SendBytes("[OK] Shunt Set\r\n", sizeof("[OK] Shunt Set\r\n") - 1);
        }
        else
        {
            COM2_SendBytes("[ERR] Invalid Shunt (>0uOhm)\r\n",
                           sizeof("[ERR] Invalid Shunt (>0uOhm)\r\n") - 1);
        }
    }
    else if (strncmp(cmd, "CMD:AVG=", 8) == 0)
    {
        int32_t val = parse_int(cmd + 8);
        if (val == 1 || val == 4 || val == 16 || val == 64 ||
            val == 128 || val == 256 || val == 512 || val == 1024)
        {
            g_dongle_cfg.avg_samples = (uint16_t)val;
            s_pending_cmd.opcode = CTRL_OP_SET_CFG;
            s_pending_cmd.param8 = (uint8_t)(g_dongle_cfg.report_interval_ms / 10);
            s_pending_cmd.param16 = g_dongle_cfg.avg_samples;
            s_pending_cmd.param32 = g_dongle_cfg.shunt_uohm;
            s_pending_cmd.full_scale_ma = g_dongle_cfg.full_scale_ma;
            s_has_pending_cmd = 1;
            Dongle_Config_Save();
            COM2_SendBytes("[OK] Avg Set\r\n", sizeof("[OK] Avg Set\r\n") - 1);
        }
        else
        {
            COM2_SendBytes("[ERR] Invalid Avg (1/4/16/64/128/256/512/1024)\r\n",
                           sizeof("[ERR] Invalid Avg (1/4/16/64/128/256/512/1024)\r\n") - 1);
        }
    }
    else if (strcmp(cmd, "CMD:SAVE") == 0)
    {
        s_pending_cmd.opcode = CTRL_OP_SET_CFG;
        s_pending_cmd.param8 = (uint8_t)(g_dongle_cfg.report_interval_ms / 10);
        s_pending_cmd.param16 = g_dongle_cfg.avg_samples;
        s_pending_cmd.param32 = g_dongle_cfg.shunt_uohm;
            s_pending_cmd.full_scale_ma = g_dongle_cfg.full_scale_ma;
        s_pending_cmd.full_scale_ma = g_dongle_cfg.full_scale_ma;
        s_has_pending_cmd = 1;
        Dongle_Config_Save();
        COM2_SendBytes("[OK] Saved to Flash\r\n", sizeof("[OK] Saved to Flash\r\n") - 1);
    }
    else if (strcmp(cmd, "CMD:CFG?") == 0)
    {
        char out[80];
        int l = sprintf(out, "[CFG] RATE=%dms, FSC=%dmA, AVG=%d, SHUNT=%duOhm, SWAP=%d, LINKLED=%d, BLE=%dHz\r\n",
                        (int)g_dongle_cfg.report_interval_ms,
                        (int)g_dongle_cfg.full_scale_ma,
                        (int)g_dongle_cfg.avg_samples,
                        (int)g_dongle_cfg.shunt_uohm,
                        (int)g_dongle_cfg.uart_swapped,
                        (int)g_dongle_cfg.link_led_max_duty,
                        (int)g_dongle_cfg.ble_adv_hz);
        COM2_SendBytes(out, l);
    }
    else if (strcmp(cmd, "CMD:VER?") == 0 || strcmp(cmd, "CMD:VER") == 0)
    {
        char out[64];
        int l = sprintf(out, "[VER] Dongle: %s\r\n", FW_VERSION_STR);
        COM2_SendBytes(out, l);

        if (RF_bound_Flag)
        {
            /* Ask the Probe for its firmware version over the 2.4G link; the
             * reply arrives asynchronously (PKT_RSP_CTRL) and is forwarded to COM2. */
            s_pending_cmd.opcode = CTRL_OP_GET_VER;
            s_pending_cmd.param8 = 0;
            s_pending_cmd.param16 = 0;
            s_pending_cmd.param32 = 0;
            s_has_pending_cmd = 1;
        }
        else
        {
            COM2_SendBytes("[VER] Probe:  <not connected>\r\n", 31);
        }
    }
    else if (strcmp(cmd, "CMD:DFU") == 0)
    {
        /* Wipe the reset vector and reset into the factory bootloader so the
         * Dongle can be re-flashed over USB ISP without opening the enclosure.
         * The two virtual COM ports disappear; if the ISP device is not seen
         * immediately, replug the USB connector (ISP pin/bus detection only
         * runs on a power-on reset), then download with WCHISPTool. */
        const char *m = "[OK] DFU reset to bootloader; replug USB if not detected, then flash via WCHISPTool\r\n";
        COM2_SendBytes(m, (uint16_t)strlen(m));
        DelayMs(120);                    /* let the host drain the notice over USB */
        if (!Dongle_EnterDFU())
        {
            COM2_SendBytes("[ERR] ISP erase failed\r\n", sizeof("[ERR] ISP erase failed\r\n") - 1);
        }
    }
    else if (strcmp(cmd, "CMD:PROBE_DFU") == 0)
    {
        if (!RF_bound_Flag || s_has_pending_cmd)
        {
            COM2_SendBytes("[ERR] Probe not ready\r\n", sizeof("[ERR] Probe not ready\r\n") - 1);
        }
        else
        {
            s_pending_cmd.opcode = CTRL_OP_PROBE_DFU;
            s_pending_cmd.param8 = 0;
            s_pending_cmd.param16 = 0;
            s_pending_cmd.param32 = 0;
            s_has_pending_cmd = 1;
            COM2_SendBytes("[OK] Probe ISP requested\r\n", sizeof("[OK] Probe ISP requested\r\n") - 1);
        }
    }
    else if (strncmp(cmd, "CMD:FW_START=", 13) == 0)
    {
        char *p = cmd + 13;
        s_fw_total_size = (uint32_t)parse_int(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        s_fw_expected_crc = (uint32_t)strtoul(p, NULL, 16);

        if (s_fw_total_size == 0 || s_fw_total_size > DONGLE_MAX_FW_SIZE)
        {
            COM2_SendBytes("[ERR] FW size invalid (max 32KB)\r\n", sizeof("[ERR] FW size invalid (max 32KB)\r\n") - 1);
        }
        else if (s_fw_in_progress)
        {
            COM2_SendBytes("[ERR] FW already in progress\r\n", sizeof("[ERR] FW already in progress\r\n") - 1);
        }
        else
        {
            /* The update transaction owns the chip.  Mask both RF interrupt
             * sources before the first erase so no RF callback can be nested
             * with Flash-ROM code or the COM2 ACK path. */
            s_fw_in_progress = 1;
            FW_SetRfPaused(1);
            uint32_t sectors = (s_fw_total_size + 4095) / 4096;
            uint8_t erase_failed = 0;
            for (uint32_t s = 0; s < sectors; s++)
            {
                uint32_t irqv;
                uint8_t rc;
                SYS_DisableAllIrq(&irqv);
                rc = FLASH_ROM_ERASE(DONGLE_SLOT_B_ADDR + s * 4096, 4096);
                SYS_RecoverIrq(irqv);
                if (rc != 0)
                {
                    erase_failed = 1;
                    break;
                }
            }
            if (erase_failed)
            {
                s_fw_in_progress = 0;
                FW_SetRfPaused(0);
                COM2_SendBytes("[ERR] FW erase failed\r\n", sizeof("[ERR] FW erase failed\r\n") - 1);
            }
            else
            {
                /* Self-update owns the device until FW_FINISH.  Drop any
                 * pending passthrough bytes and telemetry so neither the RF
                 * callback nor COM1 can contend with the updater's ACK path. */
                s_com1_rx_head = 0;
                s_com1_rx_tail = 0;
                s_com1_rx_count = 0;
                s_com1_tx_head = 0;
                s_com1_tx_tail = 0;
                s_com1_tx_count = 0;
                COM2_SendBytes("[OK] FW_START\r\n", sizeof("[OK] FW_START\r\n") - 1);
            }
        }
    }
    else if (strncmp(cmd, "CMD:FW_DATA=", 12) == 0)
    {
        if (!s_fw_in_progress)
        {
            COM2_SendBytes("[ERR] FW not started\r\n", sizeof("[ERR] FW not started\r\n") - 1);
            return;
        }
        char *p = cmd + 12;
        uint32_t offset = (uint32_t)parse_int(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;

        /* FW_DATA is handled while COM1 is quiescent. Reuse its aligned
         * static FIFO instead of putting another 128-byte buffer on the
         * already tight CH570 main-loop stack. */
        uint8_t *data_buf = s_com1_rx_fifo;
        uint32_t byte_cnt = 0;
        /* data_buf is a pointer into the COM1 FIFO; sizeof(data_buf) is only
         * the pointer width and would silently limit self-update blocks to
         * four bytes.  FW_DATA commands are bounded by the command buffer and
         * the updater sends at most 128 binary bytes per line. */
        while (*p && *(p + 1) && byte_cnt < 128u)
        {
            int h1 = hex_char_to_val(*p++);
            int h2 = hex_char_to_val(*p++);
            if (h1 < 0 || h2 < 0) break;
            data_buf[byte_cnt++] = (uint8_t)((h1 << 4) | h2);
        }

        if (byte_cnt > 0 && offset <= s_fw_total_size &&
            byte_cnt <= s_fw_total_size - offset && (offset & 3u) == 0)
        {
            uint32_t write_len = byte_cnt;
            while (write_len % 4 != 0) data_buf[write_len++] = 0xFF;

            uint32_t irqv;
            uint8_t rc;
            SYS_DisableAllIrq(&irqv);
            rc = FLASH_ROM_WRITE(DONGLE_SLOT_B_ADDR + offset, data_buf, write_len);
            SYS_RecoverIrq(irqv);
            if (rc == 0)
            {
                COM2_SendFwAck(offset + byte_cnt);
            }
            else
            {
                COM2_SendBytes("[ERR] Flash write failed\r\n", sizeof("[ERR] Flash write failed\r\n") - 1);
            }
        }
        else
        {
            COM2_SendBytes("[ERR] FW_DATA invalid\r\n", sizeof("[ERR] FW_DATA invalid\r\n") - 1);
        }
    }
    else if (strcmp(cmd, "CMD:FW_FINISH") == 0)
    {
        if (!s_fw_in_progress || s_fw_total_size == 0)
        {
            COM2_SendBytes("[ERR] FW not in progress\r\n", sizeof("[ERR] FW not in progress\r\n") - 1);
            return;
        }

        const uint8_t *staged = (const uint8_t *)DONGLE_SLOT_B_ADDR;
        uint32_t actual_crc = calc_crc32(staged, s_fw_total_size);

        if (actual_crc == s_fw_expected_crc)
        {
            COM2_SendBytes("[OK] FW_VERIFIED, APPLYING...\r\n", sizeof("[OK] FW_VERIFIED, APPLYING...\r\n") - 1);
            DelayMs(100);
            Dongle_ApplyFirmware(s_fw_total_size);
        }
        else
        {
            char err[64];
            int el = sprintf(err, "[ERR] CRC mismatch exp=0x%08X act=0x%08X\r\n",
                             (unsigned int)s_fw_expected_crc, (unsigned int)actual_crc);
            COM2_SendBytes(err, (uint16_t)el);
            s_fw_in_progress = 0;
            FW_SetRfPaused(0);
        }
    }
    else if (strcmp(cmd, "CMD:FW_ABORT") == 0)
    {
        s_fw_in_progress = 0;
        FW_SetRfPaused(0);
        COM2_SendBytes("[OK] FW_ABORTED\r\n", sizeof("[OK] FW_ABORTED\r\n") - 1);
    }
    else if (strncmp(cmd, "CMD:PROBE_FW_START=", 19) == 0)
    {
        if (!RF_bound_Flag)
        {
            COM2_SendBytes("[ERR] Probe not connected\r\n", sizeof("[ERR] Probe not connected\r\n") - 1);
            return;
        }
        if (s_probe_fw_in_progress)
        {
            COM2_SendBytes("[ERR] Probe FW in progress\r\n", sizeof("[ERR] Probe FW in progress\r\n") - 1);
            return;
        }
        char *p = cmd + 19;
        s_probe_fw_total_size = (uint32_t)parse_int(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        s_probe_fw_expected_crc = (uint32_t)strtoul(p, NULL, 16);

        if (s_probe_fw_total_size == 0 || s_probe_fw_total_size > PROBE_MAX_FW_SIZE)
        {
            COM2_SendBytes("[ERR] FW size invalid (max 32KB)\r\n", sizeof("[ERR] FW size invalid (max 32KB)\r\n") - 1);
            return;
        }

        s_probe_ota_cmd.ota_op = OTA_OP_START;
        s_probe_ota_cmd.offset = s_probe_fw_total_size;
        s_probe_ota_cmd.crc32 = s_probe_fw_expected_crc;
        s_probe_ota_cmd.chunk_idx++;
        s_probe_ota_acked = 0;
        s_probe_ota_pending = 1;

        if (Dongle_WaitProbeOtaAck(4000))
        {
            if (s_probe_ota_rsp.status == OTA_STATUS_OK)
            {
                s_probe_fw_in_progress = 1;
                COM2_SendBytes("[OK] PROBE_FW_START\r\n", sizeof("[OK] PROBE_FW_START\r\n") - 1);
            }
            else
            {
                char err[48];
                int el = sprintf(err, "[ERR] Probe start failed (%d)\r\n", (int)s_probe_ota_rsp.status);
                COM2_SendBytes(err, (uint16_t)el);
            }
        }
        else
        {
            s_probe_ota_pending = 0;
            COM2_SendBytes("[ERR] Probe start timeout\r\n", sizeof("[ERR] Probe start timeout\r\n") - 1);
        }
    }
    else if (strncmp(cmd, "CMD:PROBE_FW_DATA=", 18) == 0)
    {
        if (!s_probe_fw_in_progress)
        {
            COM2_SendBytes("[ERR] Probe FW not started\r\n", sizeof("[ERR] Probe FW not started\r\n") - 1);
            return;
        }
        char *p = cmd + 18;
        uint32_t offset = (uint32_t)parse_int(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;

        uint8_t data_buf[64];
        uint32_t byte_cnt = 0;
        while (*p && *(p + 1) && byte_cnt < sizeof(data_buf))
        {
            int h1 = hex_char_to_val(*p++);
            int h2 = hex_char_to_val(*p++);
            if (h1 < 0 || h2 < 0) break;
            data_buf[byte_cnt++] = (uint8_t)((h1 << 4) | h2);
        }

        if (byte_cnt > 0 && (offset + byte_cnt) <= s_probe_fw_total_size)
        {
            s_probe_ota_cmd.ota_op = OTA_OP_DATA;
            s_probe_ota_cmd.offset = offset;
            s_probe_ota_cmd.len = (uint8_t)byte_cnt;
            memcpy(s_probe_ota_cmd.data, data_buf, byte_cnt);
            s_probe_ota_cmd.chunk_idx++;
            s_probe_ota_acked = 0;
            s_probe_ota_pending = 1;

            if (Dongle_WaitProbeOtaAck(2000))
            {
                if (s_probe_ota_rsp.status == OTA_STATUS_OK)
                {
                    char rsp[32];
                    int rl = sprintf(rsp, "[OK] %u\r\n", (unsigned int)(offset + byte_cnt));
                    COM2_SendBytes(rsp, (uint16_t)rl);
                }
                else
                {
                    char err[48];
                    int el = sprintf(err, "[ERR] Probe write failed (%d)\r\n", (int)s_probe_ota_rsp.status);
                    COM2_SendBytes(err, (uint16_t)el);
                }
            }
            else
            {
                s_probe_ota_pending = 0;
                COM2_SendBytes("[ERR] Probe data timeout\r\n", sizeof("[ERR] Probe data timeout\r\n") - 1);
            }
        }
        else
        {
            COM2_SendBytes("[ERR] PROBE_FW_DATA invalid\r\n", sizeof("[ERR] PROBE_FW_DATA invalid\r\n") - 1);
        }
    }
    else if (strcmp(cmd, "CMD:PROBE_FW_FINISH") == 0)
    {
        if (!s_probe_fw_in_progress || s_probe_fw_total_size == 0)
        {
            COM2_SendBytes("[ERR] Probe FW not in progress\r\n", sizeof("[ERR] Probe FW not in progress\r\n") - 1);
            return;
        }

        s_probe_ota_cmd.ota_op = OTA_OP_FINISH;
        s_probe_ota_cmd.offset = s_probe_fw_total_size;
        s_probe_ota_cmd.crc32 = s_probe_fw_expected_crc;
        s_probe_ota_cmd.chunk_idx++;
        s_probe_ota_acked = 0;
        s_probe_ota_pending = 1;

        if (Dongle_WaitProbeOtaAck(4000))
        {
            if (s_probe_ota_rsp.status == OTA_STATUS_OK)
            {
                s_probe_fw_in_progress = 0;
                COM2_SendBytes("[OK] PROBE_FW_VERIFIED, APPLYING...\r\n", sizeof("[OK] PROBE_FW_VERIFIED, APPLYING...\r\n") - 1);
            }
            else
            {
                s_probe_fw_in_progress = 0;
                char err[64];
                int el = sprintf(err, "[ERR] Probe CRC mismatch act=0x%08X\r\n", (unsigned int)s_probe_ota_rsp.offset);
                COM2_SendBytes(err, (uint16_t)el);
            }
        }
        else
        {
            s_probe_ota_pending = 0;
            COM2_SendBytes("[ERR] Probe finish timeout\r\n", sizeof("[ERR] Probe finish timeout\r\n") - 1);
        }
    }
    else if (strcmp(cmd, "CMD:PROBE_FW_ABORT") == 0)
    {
        s_probe_fw_in_progress = 0;
        if (RF_bound_Flag)
        {
            s_probe_ota_cmd.ota_op = OTA_OP_ABORT;
            s_probe_ota_cmd.chunk_idx++;
            s_probe_ota_pending = 1;
        }
        COM2_SendBytes("[OK] PROBE_FW_ABORTED\r\n", sizeof("[OK] PROBE_FW_ABORTED\r\n") - 1);
    }
    else
    {
        COM2_SendBytes("[ERR] Unknown Command\r\n", sizeof("[ERR] Unknown Command\r\n") - 1);
    }
}

static void COM2_RxCmdPush(uint8_t b)
{
    if (b == '\n' || b == '\r')
    {
        if (s_com2_cmd_len > 0)
        {
            s_com2_cmd_buf[s_com2_cmd_len] = '\0';
            s_com2_cmd_ready = 1;
        }
    }
    else
    {
        if (!s_com2_cmd_ready && s_com2_cmd_len < (COM2_CMD_BUF_SIZE - 1))
        {
            s_com2_cmd_buf[s_com2_cmd_len++] = (char)b;
        }
    }
}

/* ---------------- USB Low-Level / Endpoint Handlers ---------------- */

/* Keep EP0 parsing in Flash instead of letting the compiler inline it into
 * the RAM-resident USB ISR.  An updater must mask USB interrupts while Flash
 * is busy, or provide a separate RAM-resident EP0 path. */
static __attribute__((noinline)) void USB_SetupHandler(void)
{
    uint8_t req_type = UsbSetupBuf->bRequestType & 0x60;
    uint8_t req = UsbSetupBuf->bRequest;
    s_setup_len = ((uint16_t)UsbSetupBuf->wLengthH << 8) | UsbSetupBuf->wLengthL;
    s_setup_req = req;
    s_setup_iface = UsbSetupBuf->wIndexL;

    if (req_type == 0x00) // Standard Request
    {
        switch (req)
        {
            case USB_GET_STATUS:
                Ep0Buffer[0] = 0;
                Ep0Buffer[1] = 0;
                R8_UEP0_T_LEN = (s_setup_len < 2) ? (uint8_t)s_setup_len : 2;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case USB_GET_DESCRIPTOR:
            {
                uint8_t desc_type = UsbSetupBuf->wValueH;
                uint8_t desc_idx  = UsbSetupBuf->wValueL;

                switch (desc_type)
                {
                    case 1: // Device
                        s_p_descr = TAB_USB_CDC_DEV_DES;
                        s_setup_len = (s_setup_len > sizeof(TAB_USB_CDC_DEV_DES)) ? sizeof(TAB_USB_CDC_DEV_DES) : s_setup_len;
                        break;
                    case 2: // Configuration
                        s_p_descr = TAB_USB_CDC_CFG_DES;
                        s_setup_len = (s_setup_len > sizeof(TAB_USB_CDC_CFG_DES)) ? sizeof(TAB_USB_CDC_CFG_DES) : s_setup_len;
                        break;
                    case 3: // String
                        switch (desc_idx)
                        {
                            case 0: s_p_descr = TAB_USB_LID_STR_DES; s_setup_len = (s_setup_len > sizeof(TAB_USB_LID_STR_DES)) ? sizeof(TAB_USB_LID_STR_DES) : s_setup_len; break;
                            case 1: s_p_descr = TAB_USB_VEN_STR_DES; s_setup_len = (s_setup_len > sizeof(TAB_USB_VEN_STR_DES)) ? sizeof(TAB_USB_VEN_STR_DES) : s_setup_len; break;
                            case 2: s_p_descr = TAB_USB_PRD_STR_DES; s_setup_len = (s_setup_len > sizeof(TAB_USB_PRD_STR_DES)) ? sizeof(TAB_USB_PRD_STR_DES) : s_setup_len; break;
                            case 3: s_p_descr = TAB_USB_SER_STR_DES; s_setup_len = (s_setup_len > sizeof(TAB_USB_SER_STR_DES)) ? sizeof(TAB_USB_SER_STR_DES) : s_setup_len; break;
                            case 4: s_p_descr = TAB_USB_COM1_STR_DES; s_setup_len = (s_setup_len > sizeof(TAB_USB_COM1_STR_DES)) ? sizeof(TAB_USB_COM1_STR_DES) : s_setup_len; break;
                            case 5: s_p_descr = TAB_USB_COM2_STR_DES; s_setup_len = (s_setup_len > sizeof(TAB_USB_COM2_STR_DES)) ? sizeof(TAB_USB_COM2_STR_DES) : s_setup_len; break;
                            default: s_setup_len = 0; break;
                        }
                        break;
                    default:
                        s_setup_len = 0;
                        break;
                }

                uint8_t tx_len = (s_setup_len > THIS_ENDP0_SIZE) ? THIS_ENDP0_SIZE : (uint8_t)s_setup_len;
                if (s_p_descr && tx_len > 0)
                {
                    memcpy(Ep0Buffer, s_p_descr, tx_len);
                    s_p_descr += tx_len;
                    s_setup_len -= tx_len;
                }
                R8_UEP0_T_LEN = tx_len;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            }
            case USB_SET_ADDRESS:
                s_usb_address = UsbSetupBuf->wValueL;
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case USB_GET_CONFIGURATION:
                Ep0Buffer[0] = s_usb_config;
                R8_UEP0_T_LEN = 1;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case USB_GET_INTERFACE:
                Ep0Buffer[0] = 0; // All interfaces have alternate setting 0.
                R8_UEP0_T_LEN = 1;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case USB_SET_CONFIGURATION:
                s_usb_config = UsbSetupBuf->wValueL;
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case USB_CLEAR_FEATURE:
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case USB_SET_INTERFACE:
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            default:
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_STALL | UEP_T_RES_STALL;
                break;
        }
    }
    else if (req_type == 0x20) // CDC Class-Specific Request
    {
        uint8_t iface = UsbSetupBuf->wIndexL;

        switch (req)
        {
            case 0x20: // SET_LINE_CODING
                // 7 bytes will arrive in EP0 OUT token
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case 0x21: // GET_LINE_CODING
            {
                LINE_CODE *lc = (iface == 0) ? &s_com1_line_coding : &s_com2_line_coding;
                memcpy(Ep0Buffer, lc, 7);
                R8_UEP0_T_LEN = 7;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            }
            case 0x22: // SET_CONTROL_LINE_STATE
            {
                uint8_t dtr = UsbSetupBuf->wValueL & 0x01;
                uint8_t rts = (UsbSetupBuf->wValueL & 0x02) ? 1 : 0;

                if (iface == 0) // COM1: Target Passthrough
                {
                    s_com1_line_coding.ioStaus = UsbSetupBuf->wValueL;
                    Uart0Para.ioStaus = UsbSetupBuf->wValueL;

                    COM1_HandleControlLines((uint8_t)(dtr | (rts << 1)));
                }
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            }
            default:
                R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_STALL | UEP_T_RES_STALL;
                break;
        }
    }
    else
    {
        R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_STALL | UEP_T_RES_STALL;
    }
}

static __attribute__((noinline)) void USB_EP0_IN_Handler(void)
{
    if (s_setup_req == USB_GET_DESCRIPTOR)
    {
        uint8_t tx_len = (s_setup_len > THIS_ENDP0_SIZE) ? THIS_ENDP0_SIZE : (uint8_t)s_setup_len;
        if (tx_len > 0)
        {
            memcpy(Ep0Buffer, s_p_descr, tx_len);
            s_p_descr += tx_len;
            s_setup_len -= tx_len;
            R8_UEP0_T_LEN = tx_len;
            R8_UEP0_CTRL ^= RB_UEP_T_TOG; // Next packet alternates DATA1/DATA0.
        }
        else
        {
            R8_UEP0_T_LEN = 0;
            R8_UEP0_CTRL = RB_UEP_R_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
        }
    }
    else if (s_setup_req == USB_SET_ADDRESS)
    {
        R8_USB_DEV_AD = s_usb_address;
        R8_UEP0_CTRL = RB_UEP_R_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
    }
    else
    {
        R8_UEP0_CTRL = RB_UEP_R_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
    }
}

static __attribute__((noinline)) void USB_EP0_OUT_Handler(void)
{
    if (s_setup_req == 0x20) // SET_LINE_CODING received
    {
        LINE_CODE *lc = (s_setup_iface == 0) ? &s_com1_line_coding : &s_com2_line_coding;
        memcpy(lc, Ep0Buffer, 7);

        if (s_setup_iface == 0)
        {
            Uart0Para.BaudRate = s_com1_line_coding.BaudRate;
            Uart0Para.StopBits = s_com1_line_coding.StopBits;
            Uart0Para.ParityType = s_com1_line_coding.ParityType;
            Uart0Para.DataBits = s_com1_line_coding.DataBits;
            UART_Status = 1; // notify RF of baud rate change
        }

        R8_UEP0_T_LEN = 0;
        R8_UEP0_CTRL = EP0_DATA1 | UEP_R_RES_ACK | UEP_T_RES_ACK;
    }
    else
    {
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    }
}

__INTERRUPT
__HIGH_CODE
void USB_IRQHandler(void)
{
    if (R8_USB_INT_FG & RB_UIF_TRANSFER)
    {
        uint8_t int_st = R8_USB_INT_ST;
        /* WCH reports SETUP through RB_UIS_SETUP_ACT.  The token field can
         * already be in its free state, so it is not a reliable SETUP test. */
        if (int_st & RB_UIS_SETUP_ACT)
        {
            USB_SetupHandler();
        }
        else if ((int_st & MASK_UIS_TOKEN) != MASK_UIS_TOKEN)
        {
            uint8_t token_ep = int_st & (MASK_UIS_TOKEN | MASK_UIS_ENDP);
            uint8_t rx_len   = R8_USB_RX_LEN;

            switch (token_ep)
            {
                case (UIS_TOKEN_OUT | 1): // COM1 Data from PC
                    if (R8_USB_INT_FG & RB_U_TOG_OK)
                    {
                        R8_UEP1_CTRL ^= RB_UEP_R_TOG;
                        for (uint8_t i = 0; i < rx_len; i++)
                        {
                            COM1_RxPush(Ep1Buffer[i]);
                        }
                        if ((COM1_RX_FIFO_SIZE - s_com1_rx_count) < 128)
                        {
                            R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_NAK;
                        }
                    }
                    break;

                case (UIS_TOKEN_IN | 1): // COM1 Data to PC
                    R8_UEP1_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                    s_ep1_in_busy = 0;
                    COM1_CheckTxToHost();
                    break;

                case (UIS_TOKEN_OUT | 2): // COM2 Command from PC
                    if (R8_USB_INT_FG & RB_U_TOG_OK)
                    {
                        R8_UEP2_CTRL ^= RB_UEP_R_TOG;
                        for (uint8_t i = 0; i < rx_len; i++)
                        {
                            COM2_RxCmdPush(Ep2Buffer[i]);
                        }
                    }
                    break;

                case (UIS_TOKEN_IN | 2): // COM2 Telemetry to PC
                    R8_UEP2_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                    s_ep2_in_busy = 0;
                    COM2_CheckTxToHost();
                    break;

                case (UIS_TOKEN_IN | 3): // COM2 Notification IN
                    R8_UEP3_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP3_CTRL = (R8_UEP3_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                    break;

                case (UIS_TOKEN_IN | 4): // COM1 Notification IN
                    R8_UEP4_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP4_CTRL = (R8_UEP4_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                    break;

                case (UIS_TOKEN_IN | 0):
                    USB_EP0_IN_Handler();
                    break;

                case (UIS_TOKEN_OUT | 0):
                    USB_EP0_OUT_Handler();
                    break;
            }
        }
        R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
    if (R8_USB_INT_FG & RB_UIF_BUS_RST)
    {
        s_usb_address = 0;
        s_usb_config  = 0;
        s_ep1_in_busy = 0;
        s_ep2_in_busy = 0;
        s_com1_rx_count = 0;
        s_com1_rx_head = 0;
        s_com1_rx_tail = 0;
        s_dtr_rts_initialized = 0;
        s_dtr_rts_candidate = RESET_CANDIDATE_NONE;
        s_dtr_rts_boot_active = 0;
        s_reset_event_head = 0;
        s_reset_event_count = 0;
        R8_USB_DEV_AD = 0x00;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP3_CTRL = UEP_T_RES_NAK;
        R8_UEP4_CTRL = UEP_T_RES_NAK;
        R8_USB_INT_FG = RB_UIF_BUS_RST;
    }
    if (R8_USB_INT_FG & RB_UIF_SUSPEND)
    {
        R8_USB_INT_FG = RB_UIF_SUSPEND;
    }
}

void USB_Init(void)
{
    Dongle_Config_Load();
    /* Select the USB analog pins and D+ pull-up, as in WCH's USB_DeviceInit.
     * Without RB_PIN_USB_EN the host cannot see the device attach signal. */
    R16_PIN_ALTERNATE = (R16_PIN_ALTERNATE & ~RB_PIN_DEBUG_EN) |
                        RB_PIN_USB_EN | RB_UDP_PU_EN;

    R8_USB_CTRL = 0x00;

    // Endpoints: EP4 IN, EP1 OUT+IN, EP2 OUT+IN, EP3 IN
    R8_UEP4_1_MOD = RB_UEP4_TX_EN | RB_UEP1_TX_EN | RB_UEP1_RX_EN;
    R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP2_TX_EN | RB_UEP3_TX_EN;

    R16_UEP0_DMA = (UINT32)Ep0Buffer;
    R16_UEP1_DMA = (UINT32)Ep1Buffer;
    R16_UEP2_DMA = (UINT32)Ep2Buffer;
    R16_UEP3_DMA = (UINT32)Ep3Buffer;

    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP3_CTRL = UEP_T_RES_NAK;
    R8_UEP4_CTRL = UEP_T_RES_NAK;

    R8_USB_DEV_AD = 0x00;
    R8_UDEV_CTRL = RB_UD_PD_DIS;
    R8_USB_CTRL = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;
    R8_USB_INT_FG = 0xFF;
    R8_USB_INT_EN = RB_UIE_SUSPEND | RB_UIE_TRANSFER | RB_UIE_BUS_RST;

    PFIC_EnableIRQ(USB_IRQn);
    R8_UDEV_CTRL |= RB_UD_PORT_EN;
}

void USB_StatusQuery(void)
{
    if (!USB_FirmwareUpdateActive()) RF_ProcessTelemetry();
    if (s_com2_cmd_ready)
    {
        COM2_ProcessCommand(s_com2_cmd_buf);
        s_com2_cmd_len = 0;
        s_com2_cmd_ready = 0;
    }
    uint32_t irqv;
    SYS_DisableAllIrq(&irqv);
    if (!USB_FirmwareUpdateActive()) COM1_CheckTxToHost();
    COM2_CheckTxToHost();
    SYS_RecoverIrq(irqv);
    if (!USB_FirmwareUpdateActive())
    {
        LED1_Poll();
        LED2_Poll();
    }
    if (s_led_test_mode == 0)
    {
#if LED_SOFTWARE_PWM_FALLBACK
        LED_SoftwarePwmOutput(SYS_GetSysTickCnt());
#endif
    }
}

/* ---------------- LED 1 link / comm-status indicator ---------------- */
/* PA3 is PWM3 on CH570.  The LED is wired from VCC through the LED/resistor
 * into PA3, so it is active low and must sink current. */
static uint32_t s_led1_last_tick = 0;
static uint32_t s_led1_phase_tick = 0;
static uint32_t s_led1_phase_q16 = 0;
static uint32_t s_led1_activity_tick = 0;
static uint32_t s_led1_last_activity = 0;
static uint32_t s_led1_buckets[5] = { 0, 0, 0, 0, 0 };
static uint8_t  s_led1_bucket_index = 0;
static uint8_t  s_led1_level = 0;
static uint8_t  s_led1_initialized = 0;
static uint8_t  s_led1_last_duty = 0xFE;

/* A gamma-shaped table looks like a smooth optical ramp on the active-low
 * indicator.  The table is in flash and is traversed in both directions. */
static const uint8_t s_led1_breath_lut[33] = {
    0, 1, 3, 6, 10, 16, 24, 34, 46, 60, 76, 94, 113, 133, 153, 173,
    192, 209, 224, 237, 247, 253, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255
};

/* The board has active-low LEDs on PA2/PA3. Both outputs use the CH57x
 * hardware PWM peripheral in 16-bit mode; the application keeps normalized
 * duty values and converts them to the configured PWM cycle. */
static uint8_t s_led_gpio_initialized = 0;
static uint16_t s_led1_duty = 0;
static uint16_t s_led2_duty = 0;
static uint32_t s_led2_last_tick = 0;

/* Timer-driven software PWM.  The carrier has 64 instantaneous levels; the
 * six fractional bits are distributed over successive carrier periods, so
 * the requested duty still has 12-bit effective resolution without a MHz-rate
 * interrupt. */
__INTERRUPT
__HIGH_CODE
void TMR_IRQHandler(void)
{
    static uint8_t level1 = 0;
    static uint8_t level2 = 0;
    static uint8_t frac1 = 0;
    static uint8_t frac2 = 0;

    if (R8_TMR_INT_FLAG & RB_TMR_IF_CYC_END)
    {
        R8_TMR_INT_FLAG = RB_TMR_IF_CYC_END;
        if (s_led_sw_phase == 0)
        {
            uint16_t d1 = s_led_sw_duty1;
            uint16_t d2 = s_led_sw_duty2;
            uint8_t base1 = (uint8_t)(d1 >> 6);
            uint8_t base2 = (uint8_t)(d2 >> 6);
            frac1 = (uint8_t)(frac1 + (d1 & 0x3Fu));
            frac2 = (uint8_t)(frac2 + (d2 & 0x3Fu));
            level1 = (uint8_t)(base1 + (frac1 >= 64u));
            level2 = (uint8_t)(base2 + (frac2 >= 64u));
            if (frac1 >= 64u) frac1 = (uint8_t)(frac1 - 64u);
            if (frac2 >= 64u) frac2 = (uint8_t)(frac2 - 64u);
            if (level1 > LED_SOFTWARE_PWM_STEPS) level1 = LED_SOFTWARE_PWM_STEPS;
            if (level2 > LED_SOFTWARE_PWM_STEPS) level2 = LED_SOFTWARE_PWM_STEPS;
        }

        s_led_sw_phase = (uint8_t)((s_led_sw_phase + 1u) & (LED_SOFTWARE_PWM_STEPS - 1u));
        uint32_t on = 0;
        if (s_led_sw_phase < level2) on |= LED2_PIN;
        if (s_led_sw_phase < level1) on |= LED_PIN;
        GPIOA_ResetBits(on);
        GPIOA_SetBits((LED2_PIN | LED_PIN) & ~on);
    }
}

/* CH57x 16-bit PWM compares PWM_DATA against PWM_CYCLE+1. Keep the
 * application-facing duty in a normalized 0..65535 range, then convert it
 * to the actual compare range before touching the peripheral register. */
static uint16_t LED_PwmCompare(uint16_t duty)
{
    uint32_t compare = ((uint32_t)duty * (LED_PWM_CYCLE + 1u) + 32767u) / 65535u;
    if (compare > LED_PWM_CYCLE) compare = LED_PWM_CYCLE;
    return (uint16_t)compare;
}

/* The CH570 PWM control bytes do not have reliable read-back on all silicon
 * revisions.  Do not use read-modify-write on these registers: a stale read
 * can silently clear OUT_EN or leave the block in GPIO-level output. */
static void LED_PWM_Write(uint16_t duty2, uint16_t duty3)
{
    R8_SLP_CLK_OFF1 &= (uint8_t)~RB_SLP_CLK_PWMX;
    /* Follow the WCH 16-bit PWM example.  PWM_CONFIG contains both mode
     * fields and status bits on some CH57x revisions, so replacing the whole
     * byte can clear the output state. */
    PWMX_CLKCfg(8);
    PWM_16bit_CycleEnable();
    PWMX_16bit_CycleCfg(CH_PWM2 | CH_PWM3, LED_PWM_CYCLE);
    /* The schematic is VCC -> LED -> resistor -> PA pin: low level lights it.
     * Low_Level means default high and low effective in the WCH driver. */
    PWMX_16bit_ACTOUT(CH_PWM2, duty2, Low_Level, ENABLE);
    PWMX_16bit_ACTOUT(CH_PWM3, duty3, Low_Level, ENABLE);
    /* Low_Level is enum value 1 on CH57x and sets the polarity bits: the
     * board sinks current when the PWM output is low.  Keep this explicit and
     * avoid a packed control-word write that can affect unrelated channels. */
    R8_PWM_POLAR |= (uint8_t)(CH_PWM2 | CH_PWM3);
    R8_PWM_OUT_EN |= (uint8_t)(CH_PWM2 | CH_PWM3);
}

static void LED_PWM8_TestWrite(void)
{
    R8_SLP_CLK_OFF1 &= (uint8_t)~RB_SLP_CLK_PWMX;
    R16_PWM_CLOCK_DIV = 100;
    R8_PWM_CONFIG = 0x00; /* 8-bit, 256-count cycle */
    R8_PWM2_DATA = 128;
    R8_PWM3_DATA = 128;
    R8_PWM_POLAR = (uint8_t)(CH_PWM2 | CH_PWM3);
    R8_PWM_OUT_EN = (uint8_t)(CH_PWM2 | CH_PWM3);
}

/* RF startup/low-power paths may reset the PWM block. Reassert the small
 * amount of PWM state periodically so an RF event cannot leave the LED pins
 * at a static GPIO level. */
static void LED_PWM_UpdateDuty(void)
{
#if LED_SOFTWARE_PWM_FALLBACK
    s_led_sw_duty1 = (uint16_t)(((uint32_t)s_led1_duty * 4095u + 32767u) / 65535u);
    s_led_sw_duty2 = (uint16_t)(((uint32_t)s_led2_duty * 4095u + 32767u) / 65535u);
#else
    /* Reinitializing the PWM counter every few milliseconds causes visible
     * carrier phase steps during RF traffic.  Compare registers are double
     * buffered by the peripheral and can be changed in place. */
    R16_PWM2_DATA = LED_PwmCompare(s_led2_duty);
    R16_PWM3_DATA = LED_PwmCompare(s_led1_duty);
#endif
}

static void LED_GPIO_Init(void)
{
    if (s_led_gpio_initialized) return;
    GPIOA_ModeCfg(LED2_PIN | LED_PIN, GPIO_ModeOut_PP_5mA);
    /* The schematic is active-low.  Set the GPIO latch high before any PWM
     * experiment so a failed peripheral takeover cannot leave both LEDs on. */
    GPIOA_SetBits(LED2_PIN | LED_PIN);
    /* PA2/PA3 are the default PWM2/PWM3 pins.  Leave PIN_ALTERNATE untouched
     * here, matching WCH's PWMX example; this register also controls the
     * GPIO analog/digital gate and is not a PWM route selector. */
    /* RF startup can leave the PWMX peripheral clock gated. */
    R8_SLP_CLK_OFF1 &= (uint8_t)~RB_SLP_CLK_PWMX;
#if LED_SOFTWARE_PWM_FALLBACK
    R32_TMR_CNT_END = GetSysClock() / LED_SOFTWARE_PWM_ISR_HZ;
    R8_TMR_CTRL_MOD = RB_TMR_ALL_CLEAR;
    R8_TMR_CTRL_DMA = 0;
    R8_TMR_INT_FLAG = RB_TMR_IF_CYC_END;
    R8_TMR_INTER_EN = RB_TMR_IE_CYC_END;
    R8_TMR_CTRL_MOD = RB_TMR_COUNT_EN;
    PFIC_EnableIRQ(TMR_IRQn);
#else
    LED_PWM_Write(0, 0);
#endif
    s_led_gpio_initialized = 1;
}

void LED1_Init(void)
{
    LED_GPIO_Init();
    s_led1_duty = 0;
    s_led1_last_tick = SYS_GetSysTickCnt();
    s_led1_phase_tick = s_led1_last_tick;
    s_led1_phase_q16 = 0;
    s_led1_activity_tick = s_led1_last_tick;
    s_led1_last_activity = g_com1_activity_bytes;
    s_led1_bucket_index = 0;
    memset(s_led1_buckets, 0, sizeof(s_led1_buckets));
    s_led1_initialized = 1;
}

static void LED1_SetDuty(uint8_t duty)
{
    if (duty == s_led1_last_duty) return;
    s_led1_last_duty = duty;
    s_led1_duty = ((uint16_t)duty * 65535u) / 255u;
    LED_PWM_UpdateDuty();
}

void LED1_Poll(void)
{
    if (!s_led1_initialized) LED1_Init();
    if (s_led_test_mode) return;

    uint32_t now = SYS_GetSysTickCnt();
    uint32_t tick_1ms = GetSysClock() / 1000u;
    if (tick_1ms == 0) tick_1ms = 1;
    uint32_t max_duty = g_dongle_cfg.link_led_max_duty;
    if (max_duty < LINK_LED_MIN_DUTY) max_duty = LINK_LED_MIN_DUTY;

    if (!RF_bound_Flag)
    {
        /* Searching state is a clear 2 Hz blink, rather than a breathing ramp. */
        if ((uint32_t)(now - s_led1_last_tick) >= 250u * tick_1ms)
        {
            s_led1_last_tick = now;
            s_led1_level = !s_led1_level;
            LED1_SetDuty(s_led1_level ? (uint8_t)max_duty : 0);
        }
        s_led1_phase_tick = now;
        s_led1_phase_q16 = 0;
        s_led1_activity_tick = now;
        s_led1_last_activity = g_com1_activity_bytes;
        s_led1_bucket_index = 0;
        memset(s_led1_buckets, 0, sizeof(s_led1_buckets));
        return;
    }

    /* Keep five one-second byte buckets.  The poll loop can run much faster
     * than the bucket cadence, so rotate only after a full second has passed.
     * Rotating on a 1 ms poll interval would erase the five-second history. */
    uint32_t tick_1s = tick_1ms * 1000u;
    if (tick_1s == 0) tick_1s = 1000u;
    if ((uint32_t)(now - s_led1_activity_tick) >= tick_1s)
    {
        uint32_t elapsed = (uint32_t)(now - s_led1_activity_tick) / tick_1s;
        uint32_t delta = g_com1_activity_bytes - s_led1_last_activity;
        uint32_t steps = (elapsed > 5u) ? 5u : elapsed;
        uint32_t rotate = steps;
        while (rotate-- != 0u)
        {
            s_led1_bucket_index = (uint8_t)((s_led1_bucket_index + 1u) % 5u);
            s_led1_buckets[s_led1_bucket_index] = 0;
        }
        if (delta > 65535u) delta = 65535u;
        s_led1_buckets[s_led1_bucket_index] = delta;
        s_led1_last_activity = g_com1_activity_bytes;
        s_led1_activity_tick += steps * tick_1s;
    }

    uint32_t recent_bytes = 0;
    for (uint8_t i = 0; i < 5; i++) recent_bytes += s_led1_buckets[i];
    uint32_t period_ms;
    if (recent_bytes == 0u)
        period_ms = 4000u;
    else if (recent_bytes >= LED1_FAST_BYTES_5S)
        period_ms = 1000u;
    else
        period_ms = 3000u - (recent_bytes * 2000u / LED1_FAST_BYTES_5S);

    /* Keep phase as a fraction of one breath.  Traffic changes the period,
     * but never causes the brightness to jump back to a different point in
     * the cycle. */
    uint32_t elapsed_ms = (uint32_t)(now - s_led1_phase_tick) / tick_1ms;
    if (elapsed_ms != 0u)
    {
        uint32_t phase_ms = (elapsed_ms > 65535u) ? 65535u : elapsed_ms;
        s_led1_phase_q16 += (uint32_t)(((uint64_t)phase_ms * 65536u) / period_ms);
        s_led1_phase_q16 &= 0xFFFFu;
        s_led1_phase_tick += elapsed_ms * tick_1ms;
    }
    uint32_t lut_pos = (s_led1_phase_q16 * 64u) >> 16;
    if (lut_pos > 64u) lut_pos = 64u;
    uint32_t lut_index = (lut_pos <= 32u) ? lut_pos : (64u - lut_pos);
    uint32_t level = s_led1_breath_lut[lut_index];
    LED1_SetDuty((uint8_t)((max_duty * level) / 255u));
}

/* ---------------- LED 2 hardware PWM & 5s heartbeat ---------------- */

static uint8_t s_led2_last_duty = 0xFE;

void LED2_Init(void)
{
    LED_GPIO_Init();
    s_led2_duty = 0;
    s_led2_last_tick = SYS_GetSysTickCnt();
    s_led2_last_duty = 0xFE;
}

void LED2_Poll(void)
{
    if (s_led_test_mode) return;
    uint32_t now = SYS_GetSysTickCnt();
    uint32_t tick_1s = GetSysClock();
    uint16_t target = 0;
    if (g_current_report_tick != 0 && (uint32_t)(now - g_current_report_tick) <= 5u * tick_1s)
    {
        target = g_current_duty;
        if (target < LED_PWM_MIN_DUTY) target = LED_PWM_MIN_DUTY;
    }
    uint32_t tick_1ms = GetSysClock() / 1000u;
    if (tick_1ms == 0) tick_1ms = 1;
    uint32_t elapsed_ms = (uint32_t)(now - s_led2_last_tick) / tick_1ms;
    if (elapsed_ms == 0) return;
    s_led2_last_tick = now;

    /* First-order low-pass.  The target is updated with the telemetry packet
     * cadence, while the LED follows it with a substantially longer optical
     * time constant so ADC quantization does not become visible flicker. */
    uint32_t dt_ms = (elapsed_ms > 100u) ? 100u : elapsed_ms;
    uint32_t alpha_q16 = (dt_ms * 65535u) / (LED2_FILTER_TAU_MS + dt_ms);
    if (alpha_q16 == 0) alpha_q16 = 1;
    if (s_led2_duty < target) {
        uint32_t delta = (uint32_t)target - s_led2_duty;
        uint32_t change = (delta * alpha_q16 + 32767u) / 65535u;
        if (change == 0) change = 1;
        s_led2_duty = (change >= delta) ? target : (uint16_t)(s_led2_duty + change);
    } else if (s_led2_duty > target) {
        uint32_t delta = (uint32_t)s_led2_duty - target;
        uint32_t change = (delta * alpha_q16 + 32767u) / 65535u;
        if (change == 0) change = 1;
        s_led2_duty = (change >= delta) ? target : (uint16_t)(s_led2_duty - change);
    }
    LED_PWM_UpdateDuty();
}

static void LED_SoftwarePwmOutput(uint32_t now)
{
#if LED_SOFTWARE_PWM_FALLBACK
    /* Edges are generated by TMR_IRQHandler; the main loop must not touch the
     * LED pins or it would reintroduce timing jitter. */
    (void)now;
#else
    (void)now;
#endif
}

/* Apply a non-persistent electrical test mode.  GPIO modes prove that the
 * package and LED wiring work; fixed PWM then isolates the peripheral path
 * from the normal rate/telemetry calculations. */
static void LED_TestApply(void)
{
    LED_GPIO_Init();
    if (s_led_test_mode == 1 || s_led_test_mode == 2)
    {
#if LED_SOFTWARE_PWM_FALLBACK
        PFIC_DisableIRQ(TMR_IRQn);
#else
        R8_PWM_OUT_EN = 0;
#endif
        if (s_led_test_mode == 1)
        {
            GPIOA_ResetBits(LED2_PIN | LED_PIN); /* active-low: both on */
        }
        else
        {
            GPIOA_SetBits(LED2_PIN | LED_PIN);   /* active-low: both off */
        }
    }
    else if (s_led_test_mode == 3 || s_led_test_mode == 4)
    {
#if LED_SOFTWARE_PWM_FALLBACK
        s_led_sw_duty1 = 2048;
        s_led_sw_duty2 = 2048;
        PFIC_EnableIRQ(TMR_IRQn);
#else
        LED_PWM_Write(LED_PwmCompare(32768u), LED_PwmCompare(32768u));
#endif
    }
    else if (s_led_test_mode == 5)
    {
#if LED_SOFTWARE_PWM_FALLBACK
        s_led_sw_duty1 = 2048;
        s_led_sw_duty2 = 2048;
        PFIC_EnableIRQ(TMR_IRQn);
#else
        LED_PWM8_TestWrite();
#endif
    }
    else
    {
#if LED_SOFTWARE_PWM_FALLBACK
        PFIC_EnableIRQ(TMR_IRQn);
        LED_PWM_UpdateDuty();
#else
        LED_PWM_Write(0, 0);
#endif
        s_led1_last_duty = 0xFE;
        s_led2_last_tick = SYS_GetSysTickCnt();
    }
}

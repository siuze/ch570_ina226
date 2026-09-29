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

#define THIS_ENDP0_SIZE         64
#define MAX_PACKET_SIZE         64

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
static uint16_t s_setup_len  = 0;
static const uint8_t *s_p_descr = NULL;

/* In-flight IN flags */
static volatile uint8_t s_ep1_in_busy = 0;
static volatile uint8_t s_ep2_in_busy = 0;

/* ---------------- FIFO Buffers ---------------- */

/* COM1 FIFO: PC EP1 OUT -> MCU RF */
#define COM1_RX_FIFO_SIZE 256
static uint8_t  s_com1_rx_fifo[COM1_RX_FIFO_SIZE];
static uint16_t s_com1_rx_head = 0;
static uint16_t s_com1_rx_tail = 0;
static volatile uint16_t s_com1_rx_count = 0;

/* COM1 FIFO: MCU RF -> PC EP1 IN */
#define COM1_TX_FIFO_SIZE 256
static uint8_t  s_com1_tx_fifo[COM1_TX_FIFO_SIZE];
static uint16_t s_com1_tx_head = 0;
static uint16_t s_com1_tx_tail = 0;
static volatile uint16_t s_com1_tx_count = 0;

/* COM2 FIFO: Telemetry / Response -> PC EP2 IN */
#define COM2_TX_FIFO_SIZE 256
static uint8_t  s_com2_tx_fifo[COM2_TX_FIFO_SIZE];
static uint16_t s_com2_tx_head = 0;
static uint16_t s_com2_tx_tail = 0;
static volatile uint16_t s_com2_tx_count = 0;

/* COM2 Command line buffer (PC EP2 OUT) */
#define COM2_CMD_BUF_SIZE 64
static char    s_com2_cmd_buf[COM2_CMD_BUF_SIZE];
static uint8_t s_com2_cmd_len = 0;

/* Pending control command to Probe over RF */
static ctrl_cmd_pkt_t s_pending_cmd = { 0, 0, 0, 0 };
static volatile uint8_t s_has_pending_cmd = 0;

/* Telemetry & LED status */
uint8_t g_telemetry_in_5s = 0;
uint8_t g_current_duty = 0;
dev_config_t g_dongle_cfg = { CFG_MAGIC, 20000, 16, 100, 1000, 0, 0 };

/* Forward Declarations */
static void COM1_CheckTxToHost(void);
static void COM2_CheckTxToHost(void);
static void COM2_ProcessCommand(char *cmd);

/* ---------------- COM1 API ---------------- */

static void COM1_RxPush(uint8_t b)
{
    if (s_com1_rx_count < COM1_RX_FIFO_SIZE)
    {
        s_com1_rx_fifo[s_com1_rx_head] = b;
        s_com1_rx_head = (s_com1_rx_head + 1) % COM1_RX_FIFO_SIZE;
        s_com1_rx_count++;
    }
}

uint8_t COM1_RxPop(uint8_t *byte)
{
    if (s_com1_rx_count > 0)
    {
        *byte = s_com1_rx_fifo[s_com1_rx_tail];
        s_com1_rx_tail = (s_com1_rx_tail + 1) % COM1_RX_FIFO_SIZE;
        s_com1_rx_count--;
        return 1;
    }
    return 0;
}

uint8_t USB_RxQuery(void *buf, typeBufSize *len)
{
    uint8_t *p = (uint8_t *)buf;
    typeBufSize max_len = *len;
    typeBufSize cnt = 0;

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
    for (uint16_t i = 0; i < len; i++)
    {
        if (s_com1_tx_count < COM1_TX_FIFO_SIZE)
        {
            s_com1_tx_fifo[s_com1_tx_head] = data[i];
            s_com1_tx_head = (s_com1_tx_head + 1) % COM1_TX_FIFO_SIZE;
            s_com1_tx_count++;
        }
    }
    COM1_CheckTxToHost();
}

static void COM1_CheckTxToHost(void)
{
    if (!s_ep1_in_busy && s_com1_tx_count > 0 && s_usb_address)
    {
        uint8_t send_len = (s_com1_tx_count > 64) ? 64 : (uint8_t)s_com1_tx_count;
        for (uint8_t i = 0; i < send_len; i++)
        {
            Ep1Buffer[64 + i] = s_com1_tx_fifo[s_com1_tx_tail];
            s_com1_tx_tail = (s_com1_tx_tail + 1) % COM1_TX_FIFO_SIZE;
            s_com1_tx_count--;
        }
        s_ep1_in_busy = 1;
        R8_UEP1_T_LEN = send_len;
        R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    }
}

/* ---------------- COM2 API ---------------- */

void COM2_SendBytes(const char *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        if (s_com2_tx_count < COM2_TX_FIFO_SIZE)
        {
            s_com2_tx_fifo[s_com2_tx_head] = (uint8_t)data[i];
            s_com2_tx_head = (s_com2_tx_head + 1) % COM2_TX_FIFO_SIZE;
            s_com2_tx_count++;
        }
    }
    COM2_CheckTxToHost();
}

static void COM2_CheckTxToHost(void)
{
    if (!s_ep2_in_busy && s_com2_tx_count > 0 && s_usb_address)
    {
        uint8_t send_len = (s_com2_tx_count > 64) ? 64 : (uint8_t)s_com2_tx_count;
        for (uint8_t i = 0; i < send_len; i++)
        {
            Ep2Buffer[64 + i] = s_com2_tx_fifo[s_com2_tx_tail];
            s_com2_tx_tail = (s_com2_tx_tail + 1) % COM2_TX_FIFO_SIZE;
            s_com2_tx_count--;
        }
        s_ep2_in_busy = 1;
        R8_UEP2_T_LEN = send_len;
        R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    }
}

uint8_t COM2_HasPendingCmd(void)
{
    return s_has_pending_cmd;
}

ctrl_cmd_pkt_t* COM2_GetPendingCmd(void)
{
    return &s_pending_cmd;
}

void COM2_ClearPendingCmd(void)
{
    s_has_pending_cmd = 0;
    s_pending_cmd.opcode = 0;
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
static void Dongle_EnterDFU(void)
{
    uint32_t irqv;

    SYS_DisableAllIrq(&irqv);            /* ISRs' vectors live in sector 0 */
    FLASH_ROM_ERASE(0x00000000, 4096);   /* wipe reset vector + first 4KB of user code */
    SYS_ResetExecute();                  /* software reset -> ROM stays in ISP (blank flash) */
    while(1)
    {
    }                                    /* never reached */
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

    if (strcmp(cmd, "CMD:RST") == 0)
    {
        s_pending_cmd.opcode = CTRL_OP_RESET;
        s_has_pending_cmd = 1;
        COM2_SendBytes("OK: Target Reset\r\n", 18);
    }
    else if (strcmp(cmd, "CMD:BOOT") == 0)
    {
        s_pending_cmd.opcode = CTRL_OP_BOOT;
        s_has_pending_cmd = 1;
        COM2_SendBytes("OK: Enter Bootloader\r\n", 22);
    }
    else if (strcmp(cmd, "CMD:SWAP") == 0)
    {
        g_dongle_cfg.uart_swapped = !g_dongle_cfg.uart_swapped;
        s_pending_cmd.opcode = CTRL_OP_SWAP_UART;
        s_pending_cmd.param8 = g_dongle_cfg.uart_swapped;
        s_has_pending_cmd = 1;
        COM2_SendBytes("OK: Target UART Swapped\r\n", 25);
    }
    else if (strncmp(cmd, "CMD:FSC=", 8) == 0)
    {
        int32_t val = parse_int(cmd + 8);
        if (val > 0)
        {
            g_dongle_cfg.full_scale_ma = (uint16_t)val;
            COM2_SendBytes("OK: FSC Set\r\n", 13);
        }
        else
        {
            COM2_SendBytes("ERR: Invalid FSC\r\n", 18);
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
            s_has_pending_cmd = 1;
            COM2_SendBytes("OK: Rate Set\r\n", 14);
        }
        else
        {
            COM2_SendBytes("ERR: Invalid Rate (10-1000ms)\r\n", 31);
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
            s_has_pending_cmd = 1;
            COM2_SendBytes("OK: Shunt Set\r\n", 15);
        }
    }
    else if (strncmp(cmd, "CMD:AVG=", 8) == 0)
    {
        int32_t val = parse_int(cmd + 8);
        g_dongle_cfg.avg_samples = (uint16_t)val;
        s_pending_cmd.opcode = CTRL_OP_SET_CFG;
        s_pending_cmd.param8 = (uint8_t)(g_dongle_cfg.report_interval_ms / 10);
        s_pending_cmd.param16 = g_dongle_cfg.avg_samples;
        s_pending_cmd.param32 = g_dongle_cfg.shunt_uohm;
        s_has_pending_cmd = 1;
        COM2_SendBytes("OK: Avg Set\r\n", 13);
    }
    else if (strcmp(cmd, "CMD:SAVE") == 0)
    {
        s_pending_cmd.opcode = CTRL_OP_SET_CFG;
        s_pending_cmd.param8 = (uint8_t)(g_dongle_cfg.report_interval_ms / 10);
        s_pending_cmd.param16 = g_dongle_cfg.avg_samples;
        s_pending_cmd.param32 = g_dongle_cfg.shunt_uohm;
        s_has_pending_cmd = 1;
        COM2_SendBytes("OK: Saved to Flash\r\n", 20);
    }
    else if (strcmp(cmd, "CMD:CFG?") == 0)
    {
        char out[80];
        int l = sprintf(out, "[CFG] FSC=%dmA, RATE=%dms, SHUNT=%duOhm, SWAP=%d\r\n",
                        (int)g_dongle_cfg.full_scale_ma,
                        (int)g_dongle_cfg.report_interval_ms,
                        (int)g_dongle_cfg.shunt_uohm,
                        (int)g_dongle_cfg.uart_swapped);
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
        const char *m = "[DFU] reset to bootloader; replug USB if not detected, then flash via WCHISPTool\r\n";
        COM2_SendBytes(m, (uint16_t)strlen(m));
        DelayMs(120);                    /* let the host drain the notice over USB */
        Dongle_EnterDFU();               /* does not return */
    }
    else
    {
        COM2_SendBytes("ERR: Unknown Command\r\n", 22);
    }
}

static void COM2_RxCmdPush(uint8_t b)
{
    if (b == '\n' || b == '\r')
    {
        if (s_com2_cmd_len > 0)
        {
            s_com2_cmd_buf[s_com2_cmd_len] = '\0';
            COM2_ProcessCommand(s_com2_cmd_buf);
            s_com2_cmd_len = 0;
        }
    }
    else
    {
        if (s_com2_cmd_len < (COM2_CMD_BUF_SIZE - 1))
        {
            s_com2_cmd_buf[s_com2_cmd_len++] = (char)b;
        }
    }
}

/* ---------------- USB Low-Level / Endpoint Handlers ---------------- */

static void USB_SetupHandler(void)
{
    uint8_t req_type = UsbSetupBuf->bRequestType & 0x60;
    uint8_t req = UsbSetupBuf->bRequest;
    s_setup_len = ((uint16_t)UsbSetupBuf->wLengthH << 8) | UsbSetupBuf->wLengthL;
    s_setup_req = req;

    if (req_type == 0x00) // Standard Request
    {
        switch (req)
        {
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
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            }
            case USB_SET_ADDRESS:
                s_usb_address = UsbSetupBuf->wValueL;
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case USB_GET_CONFIGURATION:
                Ep0Buffer[0] = s_usb_config;
                R8_UEP0_T_LEN = 1;
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case USB_SET_CONFIGURATION:
                s_usb_config = UsbSetupBuf->wValueL;
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case USB_CLEAR_FEATURE:
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            default:
                R8_UEP0_CTRL = UEP_R_RES_STALL | UEP_T_RES_STALL;
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
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            case 0x21: // GET_LINE_CODING
            {
                LINE_CODE *lc = (iface == 0) ? &s_com1_line_coding : &s_com2_line_coding;
                memcpy(Ep0Buffer, lc, 7);
                R8_UEP0_T_LEN = 7;
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_ACK;
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

                    /* ESP-IDF / esptool Auto-Reset Sequence Detection:
                     * Bootloader Mode: DTR=1, RTS=0
                     * Normal Reset Mode: DTR=0, RTS=1
                     */
                    if (dtr && !rts)
                    {
                        s_pending_cmd.opcode = CTRL_OP_BOOT;
                        s_has_pending_cmd = 1;
                        PRINT("Auto-Download: CTRL_OP_BOOT\n");
                    }
                    else if (!dtr && rts)
                    {
                        s_pending_cmd.opcode = CTRL_OP_RESET;
                        s_has_pending_cmd = 1;
                        PRINT("Auto-Reset: CTRL_OP_RESET\n");
                    }
                }
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_ACK;
                break;
            }
            default:
                R8_UEP0_CTRL = UEP_R_RES_STALL | UEP_T_RES_STALL;
                break;
        }
    }
    else
    {
        R8_UEP0_CTRL = UEP_R_RES_STALL | UEP_T_RES_STALL;
    }
}

static void USB_EP0_IN_Handler(void)
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
            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_ACK;
        }
        else
        {
            R8_UEP0_T_LEN = 0;
            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        }
    }
    else if (s_setup_req == USB_SET_ADDRESS)
    {
        R8_USB_DEV_AD = s_usb_address;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    }
    else
    {
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    }
}

static void USB_EP0_OUT_Handler(void)
{
    if (s_setup_req == 0x20) // SET_LINE_CODING received
    {
        uint8_t iface = UsbSetupBuf->wIndexL;
        LINE_CODE *lc = (iface == 0) ? &s_com1_line_coding : &s_com2_line_coding;
        memcpy(lc, Ep0Buffer, 7);

        if (iface == 0)
        {
            Uart0Para.BaudRate = s_com1_line_coding.BaudRate;
            Uart0Para.StopBits = s_com1_line_coding.StopBits;
            Uart0Para.ParityType = s_com1_line_coding.ParityType;
            Uart0Para.DataBits = s_com1_line_coding.DataBits;
            UART_Status = 1; // notify RF of baud rate change
        }

        R8_UEP0_T_LEN = 0;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_ACK;
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
        if ((R8_USB_INT_ST & MASK_UIS_TOKEN) != MASK_UIS_TOKEN)
        {
            uint8_t token_ep = R8_USB_INT_ST & 0x3F;
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
                    }
                    break;

                case (UIS_TOKEN_IN | 1): // COM1 Data to PC
                    R8_UEP1_CTRL ^= RB_UEP_T_TOG;
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

                case (UIS_TOKEN_SETUP | 0):
                    USB_SetupHandler();
                    break;

                case (UIS_TOKEN_IN | 0):
                    USB_EP0_IN_Handler();
                    break;

                case (UIS_TOKEN_OUT | 0):
                    USB_EP0_OUT_Handler();
                    break;
            }
            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }
    }
    if (R8_USB_INT_FG & RB_UIF_BUS_RST)
    {
        s_usb_address = 0;
        s_usb_config  = 0;
        s_ep1_in_busy = 0;
        s_ep2_in_busy = 0;
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
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;

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
    COM1_CheckTxToHost();
    COM2_CheckTxToHost();
    LED1_Poll();
    LED2_Poll();
}

/* ---------------- LED 1 link / comm-status indicator ---------------- */
/* Steady ON while the 2.4G link is bound; 2Hz blink while searching/unbound. */
static uint32_t s_led1_last_tick = 0;
static uint8_t  s_led1_level = 0;

void LED1_Poll(void)
{
    uint32_t now = SYS_GetSysTickCnt();
    uint32_t tick_1s = GetSysClock();

    if (RF_bound_Flag)
    {
        if (!s_led1_level)
        {
            s_led1_level = 1;
            GPIOA_SetBits(LED_PIN);
        }
    }
    else
    {
        if ((now - s_led1_last_tick) >= (tick_1s / 4))
        {
            s_led1_last_tick = now;
            s_led1_level = !s_led1_level;
            if (s_led1_level)
                GPIOA_SetBits(LED_PIN);
            else
                GPIOA_ResetBits(LED_PIN);
        }
    }
}

/* ---------------- LED 2 PWM & 5s Heartbeat ---------------- */

static uint32_t s_last_heartbeat_tick = 0;
static uint8_t  s_blink_active = 0;
static uint32_t s_blink_start_tick = 0;

void LED2_PWM_Init(void)
{
    GPIOA_ModeCfg(GPIO_Pin_2, GPIO_ModeOut_PP_5mA);
    PWMX_CLKCfg(100);              // 100MHz / 100 = 1MHz
    PWMX_CycleCfg(PWMX_Cycle_256); // 1MHz / 256 ~= 3.9kHz
    PWMX_ACTOUT(CH_PWM2, 0, High_Level, ENABLE);
}

void LED2_Poll(void)
{
    uint32_t now = SYS_GetSysTickCnt();
    uint32_t tick_1s = GetSysClock(); // 1 sec

    if (s_blink_active)
    {
        // Blink lasts 50ms (tick_1s / 20)
        if ((now - s_blink_start_tick) >= (tick_1s / 20))
        {
            s_blink_active = 0;
            PWM2_ActDataWidth(g_current_duty);
        }
    }
    else
    {
        if ((now - s_last_heartbeat_tick) >= (5 * tick_1s))
        {
            s_last_heartbeat_tick = now;
            if (g_telemetry_in_5s)
            {
                g_telemetry_in_5s = 0;
                s_blink_active = 1;
                s_blink_start_tick = now;
                if (g_current_duty > 128)
                    PWM2_ActDataWidth(0);
                else
                    PWM2_ActDataWidth(255);
            }
            else
            {
                PWM2_ActDataWidth(0); // Off when idle >5s
            }
        }
    }
}

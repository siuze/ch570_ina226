/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_uart_tx.c
 * Author             : WCH / Modified for Wireless Current Sensor Tool
 * Version            : V2.0
 * Date               : 2026/09/23
 * Description        : Probe side RF transmission, INA226 telemetry, and control
 *******************************************************************************/

#include "rf.h"
#include "rf_uart_tx.h"
#include "uart.h"
#include "ina226.h"
#include "ble_adv.h"
#include "log.h"

rfTxBuf_t gTxBuf;
uint32_t gRfRxCount;
uint16_t gInterval;
uint16_t gTimeoutMax;
uint16_t gTimeout;
uint16_t gServerData;

uint8_t gTxDataSeq;
uint8_t gRfStatus;
uint8_t gBoundStatus;
uint8_t getDataProbe;
uint32_t gRfRxFlag;
rfPackage_t *pPkt_t;

dev_config_t g_dev_config;
static uint32_t s_last_telemetry_tick = 0;
static uint32_t s_unbound_since_tick = 0;
static uint32_t s_last_adv_tick = 0;
/* BLE addresses are transmitted least-significant byte first.  This produces
 * the conventional scanner display CA:57:09:1E:A2:26. */
static const uint8_t s_ble_adv_addr[6] = { 0x26, 0xA2, 0x1E, 0x09, 0x57, 0xCA };
static volatile uint8_t s_ver_rsp_pending = 0;   /* set by CTRL_OP_GET_VER, sent by RF_StatusQuery */
static volatile uint8_t s_probe_dfu_pending = 0;
static volatile uint8_t s_probe_dfu_armed = 0;
static uint32_t s_probe_dfu_acked_tick = 0;

/* RF throughput/latency test state.  State 1 streams test frames; state 2
 * has a result queued and waits for the response ACK before clearing. */
static volatile uint8_t s_rf_test_state = 0;
static uint16_t s_rf_test_duration_ms = 1000;
static uint32_t s_rf_test_start_tick = 0;
static uint32_t s_rf_test_tx_tick = 0;
static uint32_t s_rf_test_rtt_sum_us = 0;
static uint32_t s_rf_test_rtt_min_us = 0xFFFFFFFFu;
static uint32_t s_rf_test_rtt_max_us = 0;
static uint16_t s_rf_test_packets = 0;

static char *rf_test_append_u32(char *dst, uint32_t value)
{
    char tmp[10];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + (value % 10u)); value /= 10u; } while (value && n < sizeof(tmp));
    while (n) *dst++ = tmp[--n];
    return dst;
}

static void rf_test_format_msg(char *msg, uint32_t avg, uint32_t min, uint32_t max,
                               uint16_t packets)
{
    char *p = msg;
    *p++ = 'A'; p = rf_test_append_u32(p, avg); *p++ = ' ';
    *p++ = 'M'; p = rf_test_append_u32(p, min); *p++ = ' ';
    *p++ = 'X'; p = rf_test_append_u32(p, max); *p++ = ' ';
    *p++ = 'P'; p = rf_test_append_u32(p, packets); *p = '\0';
}

/* Called only after the Dongle has acknowledged the final RF response.  Once
 * sector zero is erased, no code or interrupt vector may be fetched there. */
__HIGH_CODE
static void Probe_EnterDFU(void)
{
    uint32_t irqv;
    SYS_DisableAllIrq(&irqv);
    if (FLASH_ROM_ERASE(0x00000000, 4096) == 0)
    {
        SYS_ResetExecute();
        while (1) { }
    }
    SYS_RecoverIrq(irqv);
}

/* Probe OTA state variables */
static volatile uint8_t  s_ota_in_progress = 0;
static volatile uint8_t  s_ota_apply_pending = 0;
static uint32_t          s_ota_total_size = 0;
static uint32_t          s_ota_expected_crc = 0;
static ota_rsp_pkt_t     s_ota_rsp;
static volatile uint8_t  s_ota_rsp_pending = 0;
static uint32_t          s_ota_apply_tick = 0;

extern struct simple_buf *pRfBuf;

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

#define RF_BUF_LEN 1536
#define PROBE_UART_RF_CREDIT_DEFAULT DATA_LEN_MAX_TX
#define PROBE_UART_RF_CREDIT_FAST    DATA_LEN_MAX_TX
#define RF_LINK_RX_WINDOW_US 20000u
static uint8_t rf_buf[RF_BUF_LEN] __attribute__((aligned(4)));

__HIGH_CODE
static void Probe_ApplyFirmware(uint32_t size)
{
    uint32_t irqv;
    uint8_t failed = 0;
    uint8_t *copy_buf = rf_buf;
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

        const uint8_t *src = (const uint8_t *)(PROBE_SLOT_B_ADDR + off);
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
        Probe_EnterDFU();
        while (1) { }
    }

    /* 3. Soft reset into new firmware at 0x00000000 */
    SYS_ResetExecute();
    while (1) { }
}

static void handle_ota_command(ota_cmd_pkt_t *pCmd)
{
    if (!pCmd) return;
    uint8_t status = OTA_STATUS_OK;

    switch (pCmd->ota_op)
    {
        case OTA_OP_START:
        {
            s_ota_total_size = pCmd->offset;
            s_ota_expected_crc = pCmd->crc32;
            if (s_ota_total_size == 0 || s_ota_total_size > PROBE_MAX_FW_SIZE)
            {
                status = OTA_STATUS_ERR_SIZE;
            }
            else
            {
                uint32_t sectors = (s_ota_total_size + 4095) / 4096;
                uint32_t irqv;
                SYS_DisableAllIrq(&irqv);
                for (uint32_t s = 0; s < sectors; s++)
                {
                    if (FLASH_ROM_ERASE(PROBE_SLOT_B_ADDR + s * 4096, 4096) != 0)
                    {
                        status = OTA_STATUS_ERR_ERASE;
                        break;
                    }
                }
                SYS_RecoverIrq(irqv);
                if (status == OTA_STATUS_OK)
                {
                    s_ota_in_progress = 1;
                    s_ota_apply_pending = 0;
                }
            }
            s_ota_rsp.ota_op = OTA_OP_START;
            s_ota_rsp.status = status;
            s_ota_rsp.chunk_idx = pCmd->chunk_idx;
            s_ota_rsp.offset = s_ota_total_size;
            s_ota_rsp_pending = 1;
            getDataProbe = 1;
            break;
        }
        case OTA_OP_DATA:
        {
            if (!s_ota_in_progress)
            {
                status = OTA_STATUS_ERR_STATE;
            }
            else
            {
                uint32_t offset = pCmd->offset;
                uint8_t len = pCmd->len;
                if (len == 0 || len > 64 || (offset & 3u) != 0 ||
                    offset > s_ota_total_size || len > s_ota_total_size - offset)
                {
                    status = OTA_STATUS_ERR_SIZE;
                }
                else
                {
                    uint8_t write_buf[64] __attribute__((aligned(4)));
                    memcpy(write_buf, pCmd->data, len);
                    uint32_t write_len = len;
                    while (write_len % 4 != 0) write_buf[write_len++] = 0xFF;

                    uint32_t irqv;
                    SYS_DisableAllIrq(&irqv);
                    if (FLASH_ROM_WRITE(PROBE_SLOT_B_ADDR + offset, write_buf, write_len) != 0)
                    {
                        status = OTA_STATUS_ERR_WRITE;
                    }
                    SYS_RecoverIrq(irqv);
                }
            }
            s_ota_rsp.ota_op = OTA_OP_DATA;
            s_ota_rsp.status = status;
            s_ota_rsp.chunk_idx = pCmd->chunk_idx;
            s_ota_rsp.offset = pCmd->offset + pCmd->len;
            s_ota_rsp_pending = 1;
            getDataProbe = 1;
            break;
        }
        case OTA_OP_FINISH:
        {
            if (!s_ota_in_progress)
            {
                status = OTA_STATUS_ERR_STATE;
            }
            else
            {
                uint32_t actual_crc = calc_crc32((const uint8_t *)PROBE_SLOT_B_ADDR, s_ota_total_size);
                if (actual_crc == s_ota_expected_crc)
                {
                    status = OTA_STATUS_OK;
                    s_ota_apply_pending = 1;
                }
                else
                {
                    status = OTA_STATUS_ERR_CRC;
                    s_ota_in_progress = 0;
                }
                s_ota_rsp.offset = actual_crc;
            }
            s_ota_rsp.ota_op = OTA_OP_FINISH;
            s_ota_rsp.status = status;
            s_ota_rsp.chunk_idx = pCmd->chunk_idx;
            s_ota_rsp_pending = 1;
            getDataProbe = 1;
            break;
        }
        case OTA_OP_ABORT:
        {
            s_ota_in_progress = 0;
            s_ota_apply_pending = 0;
            s_ota_rsp.ota_op = OTA_OP_ABORT;
            s_ota_rsp.status = OTA_STATUS_OK;
            s_ota_rsp.chunk_idx = pCmd->chunk_idx;
            s_ota_rsp.offset = 0;
            s_ota_rsp_pending = 1;
            getDataProbe = 1;
            break;
        }
    }
}

static void rfProcessRx( rfPackage_t *pPkt );
static void rfProcessTx( void );
static void rfProcessTimeout( void );

static void rf_uart_buf_normalize(void)
{
    if (pRfBuf && pRfBuf->write == pRfBuf->end)
        pRfBuf->write = pRfBuf->start;
    if (pRfBuf && pRfBuf->read == pRfBuf->end)
        pRfBuf->read = pRfBuf->start;
}

rfStatusCBs_t rfCBs =
{
    rfProcessRx,
    rfProcessTx,
    rfProcessTimeout,
    rfProcessTimeout,
};

static struct simple_buf rf_buffer;
struct simple_buf *pRfBuf = NULL;

uint8_t volatile RF_bound_Flag;
uint32_t ledcount = 0;

void Config_Load(void)
{
    dev_config_t *pCfg = (dev_config_t *)(CFG_FLASH_ADDR);
    if( pCfg->magic == CFG_MAGIC )
    {
        memcpy( &g_dev_config, pCfg, sizeof(dev_config_t) );
        if( g_dev_config.report_interval_ms < 50 || g_dev_config.report_interval_ms > 5000 )
        {
            g_dev_config.report_interval_ms = 100;
        }
    }
    else
    {
        g_dev_config.magic = CFG_MAGIC;
        g_dev_config.shunt_uohm = 20000;          // 20 mOhm (matches R104)
        g_dev_config.avg_samples = 64;            // 64 samples
        g_dev_config.report_interval_ms = 100;    // 100ms
        g_dev_config.full_scale_ma = 1000;        // 1000mA
        g_dev_config.uart_swapped = 0;            // Normal
        g_dev_config.link_led_max_duty = 255;
        g_dev_config.ble_adv_hz = BLE_ADV_DEFAULT_HZ;
    }
    /* Older builds used the numeric value 16 (and one early build wrote the
     * INA226 register bit mask 0x0400) as the default.  Normalize both to the
     * requested 64-sample default while preserving other explicit settings. */
    if (g_dev_config.avg_samples == 16 || g_dev_config.avg_samples == INA226_AVG_16)
        g_dev_config.avg_samples = 64;
    if (g_dev_config.ble_adv_hz < BLE_ADV_MIN_HZ || g_dev_config.ble_adv_hz > BLE_ADV_MAX_HZ)
        g_dev_config.ble_adv_hz = BLE_ADV_DEFAULT_HZ;
}

void Config_Save(void)
{
    uint32_t irqv;
    SYS_DisableAllIrq(&irqv);
    FLASH_ROM_ERASE( CFG_FLASH_ADDR, 4096 );
    FLASH_ROM_WRITE( CFG_FLASH_ADDR, &g_dev_config, sizeof(dev_config_t) );
    SYS_RecoverIrq(irqv);
}

static void rf_buffer_create(struct simple_buf **buf)
{
    *buf = simple_buf_create(&rf_buffer, rf_buf, sizeof(rf_buf) );
}

static void rf_disconnect( void )
{
    s_probe_dfu_pending = 0;
    s_ota_in_progress = 0;
    s_ota_apply_pending = 0;
    s_ota_rsp_pending = 0;
    RF_bound_Flag = 0;
    /* Do NOT call RFRole_Shut(); keep RF active for BLE advertising and re-pairing */
    gBoundStatus = BOUND_STATUS_IDLE;
    rf_tx_set_sync_word( AA );
    rf_tx_set_frequency( DEF_FREQUENCY );
    rf_rx_set_sync_word( AA );
    rf_rx_set_frequency( DEF_FREQUENCY );
    gRfStatus = RF_STATUS_IDLE;
}

__HIGH_CODE
static void rf_bound( bound_rsp_t *rsp )
{
    rfBoundInfo_t info;

    dbg_printf("\r\n[LINK] Bound established! Ch=%d, Addr=0x%08X\r\n",
               rsp->channel, (unsigned int)rsp->accessaddr);

    gTimeout = 0;
    gBoundStatus = BOUND_STATUS_WAIT;
    gInterval = rsp->interval;
    gTimeoutMax = (rsp->timeout*10/rsp->interval);
    gServerData = rsp->severData;
    rf_tx_set_sync_word( rsp->accessaddr );
    rf_tx_set_frequency( rsp->channel );
    rf_tx_set_phy_type( rsp->phy );
    rf_rx_set_sync_word( rsp->accessaddr );
    rf_rx_set_frequency( rsp->channel );
    rf_rx_set_phy_type( rsp->phy );
    info.head = 0x55aa;
    info.serverData = gServerData;
    FLASH_ROM_ERASE( BOUND_INFO_FLASH_ADDR, 4096 );
    FLASH_ROM_WRITE( BOUND_INFO_FLASH_ADDR,&info,4 );

    RF_bound_Flag = 1;
}

static void handle_control_command( ctrl_cmd_pkt_t *pCmd )
{
    if( !pCmd ) return;
    switch( pCmd->opcode )
    {
        case CTRL_OP_RESET:
            MAX811_ResetTarget();
            break;
        case CTRL_OP_BOOT:
            MAX811_BootloaderTarget();
            break;
        case CTRL_OP_SWAP_UART:
            g_dev_config.uart_swapped = pCmd->param8 ? 1 : 0;
            UART_SwapPins( g_dev_config.uart_swapped );
            Config_Save();
            break;
        case CTRL_OP_SET_CFG:
            if( pCmd->param32 > 0 ) g_dev_config.shunt_uohm = pCmd->param32;
            if( pCmd->param16 > 0 ) g_dev_config.avg_samples = pCmd->param16;
            if( pCmd->param8 > 0 )  g_dev_config.report_interval_ms = (uint16_t)pCmd->param8 * 10;
            if( pCmd->full_scale_ma > 0 ) g_dev_config.full_scale_ma = pCmd->full_scale_ma;
            if( pCmd->ble_adv_hz >= BLE_ADV_MIN_HZ && pCmd->ble_adv_hz <= BLE_ADV_MAX_HZ )
                g_dev_config.ble_adv_hz = pCmd->ble_adv_hz;
            INA226_SetConfig( g_dev_config.shunt_uohm, g_dev_config.avg_samples );
            Config_Save();
            break;
        case CTRL_OP_GET_VER:
            /* Stage a firmware-version response; RF_StatusQuery transmits it. */
            s_ver_rsp_pending = 1;
            break;
        case CTRL_OP_PROBE_DFU:
            s_probe_dfu_pending = 1;
            break;
        case CTRL_OP_RF_TEST:
            if (gBoundStatus == BOUND_STATUS_EST && s_rf_test_state == 0)
            {
                s_rf_test_duration_ms = pCmd->param16;
                if (s_rf_test_duration_ms < 100) s_rf_test_duration_ms = 100;
                if (s_rf_test_duration_ms > 5000) s_rf_test_duration_ms = 5000;
                s_rf_test_start_tick = SYS_GetSysTickCnt();
                s_rf_test_tx_tick = 0;
                s_rf_test_rtt_sum_us = 0;
                s_rf_test_rtt_min_us = 0xFFFFFFFFu;
                s_rf_test_rtt_max_us = 0;
                s_rf_test_packets = 0;
                s_rf_test_state = 1;
            }
            break;
        default:
            break;
    }
}

__HIGH_CODE
static void rfProcessRx( rfPackage_t *pPkt )
{
    if (!pPkt || pPkt->length < PKT_DATA_OFFSET || pPkt->length > DATA_LEN_MAX_RX)
    {
        gRfStatus = RF_STATUS_IDLE;
        gTxBuf.status = STA_IDLE;
        rf_rx_start(RF_LINK_RX_WINDOW_US);
        return;
    }

    /* Always clear TX busy state and link timeout on receiving any valid packet from Dongle */
    gRfStatus = RF_STATUS_IDLE;
    gTxBuf.status = STA_IDLE;
    gTimeout = 0;

    /* The Dongle may retransmit a response when the following Probe packet
     * was lost.  Do not append the same OPCODE_DATA payload to the target UART
     * twice, and do not repeat control/OTA side effects. */
    if (gTxDataSeq != 0 &&
         pPkt->seq == (uint8_t)(gTxDataSeq - 1u) &&
         (pPkt->type == PKT_CMD_RSP_STATUS ||
          pPkt->type == PKT_DATA_FLAG ||
          pPkt->type == PKT_CMD_CTRL ||
          pPkt->type == PKT_RSP_CTRL ||
          pPkt->type == PKT_RSP_OTA))
    {
        /* A lost follow-up poll can make the Dongle retransmit DATA_FLAG.
         * Do not append the SLIP bytes twice; schedule a fresh poll so the
         * Dongle can advance after its retransmit window. */
        if (pRfBuf && (pRfBuf->buf_len - pRfBuf->data_len) >= 128 &&
            (pPkt->type == PKT_DATA_FLAG ||
             (pPkt->type == PKT_CMD_RSP_STATUS &&
              pPkt->length > PKT_DATA_OFFSET &&
              ((rfRsp_t *)(pPkt + 1))->opcode == OPCODE_DATA)))
        {
            getDataProbe = 1;
        }
        rf_rx_start(RF_LINK_RX_WINDOW_US);
        return;
    }

    if( gTxDataSeq == pPkt->seq )
    {
        gTxDataSeq++;
    }
    else
    {
        /* Resync sequence counter with Dongle */
        gTxDataSeq = pPkt->seq + 1;
    }

    if( pPkt->type == PKT_DATA_RSP_ACK )
    {
        if (s_rf_test_state == 1 && gTxBuf.TxBuf[0] == PKT_DATA_FLAG &&
            gTxBuf.TxBuf[PKT_HEAD_LEN] == RF_TEST_MARK0 &&
            gTxBuf.TxBuf[PKT_HEAD_LEN + 1] == RF_TEST_MARK1 && s_rf_test_tx_tick != 0)
        {
            uint32_t ticks = (uint32_t)(SYS_GetSysTickCnt() - s_rf_test_tx_tick);
            uint32_t clock = GetSysClock();
            uint32_t rtt_us = (clock != 0) ? (uint32_t)(((uint64_t)ticks * 1000000u) / clock) : 0;
            s_rf_test_rtt_sum_us += rtt_us;
            if (rtt_us < s_rf_test_rtt_min_us) s_rf_test_rtt_min_us = rtt_us;
            if (rtt_us > s_rf_test_rtt_max_us) s_rf_test_rtt_max_us = rtt_us;
            s_rf_test_packets++;
            s_rf_test_tx_tick = 0;
        }
        if (s_rf_test_state == 2 && gTxBuf.TxBuf[0] == PKT_RSP_CTRL &&
            gTxBuf.TxBuf[PKT_HEAD_LEN] == CTRL_OP_RF_TEST)
        {
            s_rf_test_state = 0;
        }
        if( gTxBuf.TxBuf[0] == PKT_RSP_CTRL &&
            gTxBuf.TxBuf[PKT_HEAD_LEN] == CTRL_OP_PROBE_DFU )
        {
            s_probe_dfu_armed = 1;
        }
        if( gTxBuf.TxBuf[0] == PKT_RSP_OTA && s_ota_apply_pending == 1 )
        {
            s_ota_apply_pending = 2;
            s_ota_apply_tick = SYS_GetSysTickCnt();
        }
        if( pPkt->length > PKT_DATA_OFFSET+1 )
        {
            typeBufSize len;
            pPkt_t = pPkt;
            rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt_t+1);

            if( pRsp_t->opcode == OPCODE_OTA )
            {
                if (pPkt->length < PKT_DATA_OFFSET + 1 + sizeof(ota_cmd_pkt_t))
                {
                    rf_rx_start(RF_LINK_RX_WINDOW_US);
                    return;
                }
                handle_ota_command( (ota_cmd_pkt_t *)(pRsp_t->other.rspData) );
            }
            else if( pRsp_t->opcode == PKT_CMD_CTRL )
            {
                if (pPkt->length < PKT_DATA_OFFSET + 1 + sizeof(ctrl_cmd_pkt_t))
                {
                    rf_rx_start(RF_LINK_RX_WINDOW_US);
                    return;
                }
                handle_control_command( (ctrl_cmd_pkt_t *)(pRsp_t->other.rspData) );
            }
            else
            {
                len = pPkt_t->length-PKT_DATA_OFFSET-1;
                rf_uart_buf_normalize();
                gRfRxFlag = write_buf( pRfBuf, pRsp_t->other.rspData, &len );
                if( gRfRxFlag )
                {
                    R8_UART_IER |= RB_IER_THR_EMPTY;
                    if( !R8_UART_TFC )
                    {
                        PFIC_SetPendingIRQ( UART_IRQn );
                    }
                }
                if ((pRfBuf->buf_len - pRfBuf->data_len) >= 128)
                {
            getDataProbe = 1;
                }
                else
                {
                    getDataProbe = 0;
                }
            }
        }
    }
    else if( pPkt->type == PKT_CMD_BOUND_RSP )
    {
        if (pPkt->length < PKT_DATA_OFFSET + sizeof(bound_rsp_t))
        {
            rf_rx_start(RF_LINK_RX_WINDOW_US);
            return;
        }
        rf_bound( (bound_rsp_t *)(pPkt+1) );
    }
    else if( pPkt->type == PKT_CMD_CTRL )
    {
        if (pPkt->length < PKT_DATA_OFFSET + sizeof(ctrl_cmd_pkt_t))
        {
            rf_rx_start(RF_LINK_RX_WINDOW_US);
            return;
        }
        handle_control_command( (ctrl_cmd_pkt_t *)(pPkt+1) );
    }
    else if( pPkt->type == PKT_CMD_RSP_STATUS )
    {
        rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt+1);

        if( gBoundStatus == BOUND_STATUS_WAIT )
        {
            gBoundStatus = BOUND_STATUS_EST;
            UART_SetTimer( gInterval );
        }

        if( pPkt->length == PKT_DATA_OFFSET )
        {
        }
        else if( pRsp_t->opcode == OPCODE_OTA )
        {
            if (pPkt->length < PKT_DATA_OFFSET + 1 + sizeof(ota_cmd_pkt_t))
            {
                rf_rx_start(RF_LINK_RX_WINDOW_US);
                return;
            }
            handle_ota_command( (ota_cmd_pkt_t *)(pRsp_t->other.rspData) );
        }
        else if( pRsp_t->opcode == PKT_CMD_CTRL )
        {
            if (pPkt->length < PKT_DATA_OFFSET + 1 + sizeof(ctrl_cmd_pkt_t))
            {
                rf_rx_start(RF_LINK_RX_WINDOW_US);
                return;
            }
            handle_control_command( (ctrl_cmd_pkt_t *)(pRsp_t->other.rspData) );
        }
        else if( pRsp_t->opcode == OPCODE_BSP )
        {
            if (pRsp_t->buad_t.BaudRate != gBaudRate && pRsp_t->buad_t.BaudRate >= 1200 && pRsp_t->buad_t.BaudRate <= 2000000)
            {
                SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);
                mDelaymS(2);

                UART_SetBuad( pRsp_t->buad_t.BaudRate );
                if( pRsp_t->buad_t.StopBits )
                {
                    R8_UART_LCR |= RB_LCR_STOP_BIT;
                }
                else
                {
                    R8_UART_LCR &= ~RB_LCR_STOP_BIT;
                }

                if( pRsp_t->buad_t.ParityType )
                {
                    R8_UART_LCR &= ~RB_LCR_PAR_MOD;
                    R8_UART_LCR |= ((pRsp_t->buad_t.ParityType-1)&3)<<4;
                    R8_UART_LCR |= RB_LCR_PAR_EN;
                }
                else
                {
                    R8_UART_LCR &= ~RB_LCR_PAR_EN;
                }

                R8_UART_LCR &= ~RB_LCR_WORD_SZ;
                R8_UART_LCR |= (pRsp_t->buad_t.DataBits-5);
            }
        }
        else if( pRsp_t->opcode == OPCODE_DATA )
        {
            typeBufSize len;
            pPkt_t = pPkt;

            rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt_t+1);
            len = pPkt_t->length-PKT_DATA_OFFSET-1;
            rf_uart_buf_normalize();
            gRfRxFlag = write_buf( pRfBuf, pRsp_t->other.rspData, &len );
            if( gRfRxFlag )
            {
                R8_UART_IER |= RB_IER_THR_EMPTY;
                if( !R8_UART_TFC )
                {
                    PFIC_SetPendingIRQ( UART_IRQn );
                }
            }
            if ((pRfBuf->buf_len - pRfBuf->data_len) >= 128)
            {
                getDataProbe = 6;
            }
            else
            {
                getDataProbe = 0;
            }
        }
    }
}

/* Telemetry must not wait behind the 20 ms UART polling tick.  The old
 * ordering treated UART_STATUS_SEND as a GET_STATUS request and skipped the
 * telemetry branch, which stretched a nominal 100 ms report interval into
 * seconds when the RF link was busy with status polls. */
static uint8_t probe_send_telemetry_if_due(rfPackage_t *pPkt_t, uint32_t now,
                                           uint8_t probe_flow_credit)
{
    uint32_t interval_ticks = g_dev_config.report_interval_ms * (GetSysClock() / 1000u);
    if (gBoundStatus != BOUND_STATUS_EST || interval_ticks == 0 ||
        (uint32_t)(now - s_last_telemetry_tick) < interval_ticks)
    {
        return 0;
    }

    ina226_data_t idata;
    s_last_telemetry_tick = now;
    if (!INA226_ReadData(&idata)) return 0;

    telemetry_pkt_t *pTelem = (telemetry_pkt_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];
    pTelem->shunt_raw = idata.shunt_raw;
    pTelem->bus_raw = idata.bus_raw;
    pTelem->flags = 0;

    gRfStatus = RF_STATUS_TX;
    pPkt_t->type = PKT_DATA_TELEMETRY;
    pPkt_t->length = PKT_DATA_OFFSET + sizeof(telemetry_pkt_t);
    gTxBuf.status = STA_BUSY;
    pPkt_t->seq = gTxDataSeq;
    pPkt_t->resv = probe_flow_credit;
    rf_tx_start(gTxBuf.TxBuf, 60);
    return 1;
}

__HIGH_CODE
static void rfProcessTx( void )
{
    /* An advertising burst owns the RF IP; do not touch link state. */
    if( BLE_AdvActive() )
    {
        BLE_AdvMarkDone();
        return;
    }
    if( gRfStatus == RF_STATUS_RETX )
    {
        gRfStatus = RF_STATUS_REWAIT;
    }
    else
    {
        gRfStatus = RF_STATUS_WAITRSP;
    }
    rf_rx_start( RF_LINK_RX_WINDOW_US );
}

__HIGH_CODE
static void rfProcessTimeout( void )
{
    if( BLE_AdvActive() )
    {
        BLE_AdvMarkDone();
        return;
    }
    if( gBoundStatus == BOUND_STATUS_IDLE )
    {
        /* In pairing mode: wait for periodic 20ms timer tick instead of storming retries */
        gTxBuf.status = STA_IDLE;
        gRfStatus = RF_STATUS_IDLE;
        return;
    }

    if( gBoundStatus == BOUND_STATUS_WAIT )
    {
        if( ++gTimeout > BOUND_EST_COUNT )
        {
            dbg_printf("\r\n[LINK] Wait timeout, disconnecting to Ch 17\r\n");
            rf_disconnect();
            gTxBuf.status = STA_IDLE;
            return;
        }
    }
    else if( gBoundStatus == BOUND_STATUS_EST )
    {
        if( s_ota_in_progress )
        {
            gTimeout = 0;
        }
        else if( ++gTimeout > 25 ) /* ~2.5s without Dongle -> disconnect to Ch 17 & BLE beacon */
        {
            dbg_printf("\r\n[LINK] Link lost, disconnecting to Ch 17\r\n");
            rf_disconnect();
            gTxBuf.status = STA_IDLE;
            return;
        }
    }

    gTxBuf.status = STA_RESEND;
    if( gRfStatus == RF_STATUS_WAITRSP )
    {
        /* Allow a noisy RF window to recover without abandoning the UART
         * stream. The link watchdog remains at one second. */
        gTxBuf.resendCount = 20;
    }
}

void RF_StatusQuery( void )
{
    uint8_t s;

    if( s_probe_dfu_armed == 1 )
    {
        s_probe_dfu_acked_tick = SYS_GetSysTickCnt();
        s_probe_dfu_armed = 2;
    }
    if( s_probe_dfu_armed == 2 &&
        (uint32_t)(SYS_GetSysTickCnt() - s_probe_dfu_acked_tick) >= GetSysClock() / 10 )
    {
        s_probe_dfu_armed = 0;
        Probe_EnterDFU();
    }

    if( s_ota_apply_pending == 2 &&
        (uint32_t)(SYS_GetSysTickCnt() - s_ota_apply_tick) >= (GetSysClock() / 20) )
    {
        s_ota_apply_pending = 0;
        Probe_ApplyFirmware( s_ota_total_size );
    }

    /* A RF transaction can span several 20 ms receive windows when the
     * channel retries.  Do not abandon it after one window: doing so starts a
     * second GET_STATUS request while the first response is still in flight
     * and can duplicate a ROM SLIP frame. */
    static uint32_t s_busy_start_tick = 0;
    if( gTxBuf.status == STA_BUSY )
    {
        uint32_t now = SYS_GetSysTickCnt();
        if( s_busy_start_tick == 0 )
        {
            s_busy_start_tick = now;
        }
        else if( (uint32_t)(now - s_busy_start_tick) >= (GetSysClock() / 2) ) // 500ms
        {
            s_busy_start_tick = 0;
            gTxBuf.status = STA_IDLE;
            gRfStatus = RF_STATUS_IDLE;
            /* Recover a lost RFTEST marker ACK instead of leaving the test
             * permanently waiting for a timestamp that will never clear. */
            if (s_rf_test_state == 1 && s_rf_test_tx_tick != 0)
                s_rf_test_tx_tick = 0;
            else if (s_rf_test_state == 2)
                s_rf_test_state = 0;
            if( gBoundStatus == BOUND_STATUS_EST )
            {
                /* RFTEST is bounded to five seconds; its marker retries
                 * must not consume the normal link watchdog budget. */
                if (s_rf_test_state != 0)
                    gTimeout = 0;
                else if( ++gTimeout > gTimeoutMax )
                {
                    dbg_printf("\r\n[LINK] Link watchdog, disconnecting to Ch 17\r\n");
                    rf_disconnect();
                    return;
                }
            }
            rf_rx_start( RF_LINK_RX_WINDOW_US );
            return;
        }
        return;
    }
    else
    {
        s_busy_start_tick = 0;
    }

    if( gTxBuf.status == STA_IDLE )
    {
        /* Periodic BLE advertising beacon: broadcasts 4 times per second (250ms)
         * so both the Upper Computer (even without Dongle) and phone scanners
         * receive real-time telemetry. Only bursts when RF is STA_IDLE. */
        if( !s_ota_in_progress && !RF_bound_Flag )
        {
            uint32_t now = SYS_GetSysTickCnt();
            uint32_t tick_1ms = GetSysClock() / 1000;
            uint8_t adv_hz = g_dev_config.ble_adv_hz;
            if (adv_hz < BLE_ADV_MIN_HZ || adv_hz > BLE_ADV_MAX_HZ) adv_hz = BLE_ADV_DEFAULT_HZ;
            uint32_t adv_period_ms = 1000u / adv_hz;
            if( (now - s_last_adv_tick) >= (adv_period_ms * tick_1ms) )
            {
                ina226_data_t idata;
                s_last_adv_tick = now;
                if( INA226_ReadData(&idata) )
                {
                    BLE_AdvSetTelemetryFixed( idata.current_ua, idata.bus_uv,
                                              idata.power_mw );
                }
                BLE_AdvBurst();
                return;
            }
        }

        rfPackage_t *pPkt_t = (rfPackage_t *)gTxBuf.TxBuf;
        uint16_t free_rf_buf = pRfBuf->buf_len - pRfBuf->data_len;
        /* At 115200 baud the 128-byte window leaves enough time for the
         * target UART to drain a packet before the next RF transaction.  At
         * 460800 and above, use the full RF payload so the 10 ms link cycle
         * is not the dominant limit during ESP ROM flashing. */
        const uint16_t credit_limit = (gBaudRate >= 460800u) ?
                                      PROBE_UART_RF_CREDIT_FAST :
                                      PROBE_UART_RF_CREDIT_DEFAULT;
        uint8_t probe_flow_credit;
        if (free_rf_buf < credit_limit)
        {
            probe_flow_credit = 0; /* Buffer cannot safely accept a packet */
        }
        else
        {
            probe_flow_credit = (free_rf_buf < credit_limit) ?
                                (uint8_t)free_rf_buf : (uint8_t)credit_limit;
        }

        if( s_ota_rsp_pending && gBoundStatus == BOUND_STATUS_EST )
        {
            s_ota_rsp_pending = 0;
            ota_rsp_pkt_t *pRsp = (ota_rsp_pkt_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];
            *pRsp = s_ota_rsp;
            gRfStatus = RF_STATUS_TX;
            pPkt_t->type = PKT_RSP_OTA;
            pPkt_t->length = PKT_DATA_OFFSET + sizeof(ota_rsp_pkt_t);
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start( gTxBuf.TxBuf, 60 );
            return;
        }

        if( s_ota_in_progress && gBoundStatus == BOUND_STATUS_EST )
        {
            gRfStatus = RF_STATUS_GETS;
            pPkt_t->type = PKT_CMD_GET_STATUS;
            pPkt_t->length = PKT_DATA_OFFSET;
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start( gTxBuf.TxBuf, 60 );
            return;
        }

        if( s_probe_dfu_pending && gBoundStatus == BOUND_STATUS_EST )
        {
            ctrl_rsp_pkt_t *pRspCtrl = (ctrl_rsp_pkt_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];
            s_probe_dfu_pending = 0;
            pRspCtrl->opcode = CTRL_OP_PROBE_DFU;
            pRspCtrl->status = 0;
            pRspCtrl->resv = 0;
            strcpy(pRspCtrl->msg, "Probe entering ISP");
            gRfStatus = RF_STATUS_TX;
            pPkt_t->type = PKT_RSP_CTRL;
            pPkt_t->length = PKT_DATA_OFFSET + sizeof(ctrl_rsp_pkt_t);
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start(gTxBuf.TxBuf, 60);
            return;
        }

        if (s_rf_test_state == 1 && gBoundStatus == BOUND_STATUS_EST)
        {
            uint32_t now = SYS_GetSysTickCnt();
            uint32_t tick_1ms = GetSysClock() / 1000u;
            if (tick_1ms == 0) tick_1ms = 1;
            if ((uint32_t)(now - s_rf_test_start_tick) / tick_1ms >= s_rf_test_duration_ms &&
                s_rf_test_tx_tick == 0)
            {
                ctrl_rsp_pkt_t *pRspCtrl = (ctrl_rsp_pkt_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];
                uint32_t avg_us = s_rf_test_packets ? (s_rf_test_rtt_sum_us / s_rf_test_packets) : 0;
                s_rf_test_state = 2;
                pRspCtrl->opcode = CTRL_OP_RF_TEST;
                pRspCtrl->status = 0;
                pRspCtrl->resv = 0;
                rf_test_format_msg(pRspCtrl->msg, avg_us,
                                   (s_rf_test_rtt_min_us == 0xFFFFFFFFu) ? 0 : s_rf_test_rtt_min_us,
                                   s_rf_test_rtt_max_us, s_rf_test_packets);
                gRfStatus = RF_STATUS_TX;
                pPkt_t->type = PKT_RSP_CTRL;
                pPkt_t->length = PKT_DATA_OFFSET + sizeof(ctrl_rsp_pkt_t);
                gTxBuf.status = STA_BUSY;
                pPkt_t->seq = gTxDataSeq;
                pPkt_t->resv = 0;
                rf_tx_start(gTxBuf.TxBuf, 60);
                return;
            }

            if (s_rf_test_tx_tick == 0)
            {
                uint8_t *test = (uint8_t *)(pPkt_t + 1);
                for (uint16_t i = 0; i < RF_TEST_PAYLOAD_LEN; i++)
                    test[i] = (uint8_t)(i + 0x31u);
                test[0] = RF_TEST_MARK0;
                test[1] = RF_TEST_MARK1;
                gRfStatus = RF_STATUS_TX;
                pPkt_t->type = PKT_DATA_FLAG;
                pPkt_t->length = PKT_DATA_OFFSET + RF_TEST_PAYLOAD_LEN;
                gTxBuf.status = STA_BUSY;
                pPkt_t->seq = gTxDataSeq;
                pPkt_t->resv = 0;
                s_rf_test_tx_tick = now;
                rf_tx_start(gTxBuf.TxBuf, 60);
                return;
            }
        }

        /* Firmware-version response takes priority; send it once when staged. */
        if( s_ver_rsp_pending && gBoundStatus == BOUND_STATUS_EST )
        {
            ctrl_rsp_pkt_t *pRspCtrl = (ctrl_rsp_pkt_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];
            s_ver_rsp_pending = 0;
            pRspCtrl->opcode = CTRL_OP_GET_VER;
            pRspCtrl->status = 0;
            pRspCtrl->resv = 0;
            strncpy( pRspCtrl->msg, FW_VERSION_STR, sizeof(pRspCtrl->msg) - 1 );
            pRspCtrl->msg[sizeof(pRspCtrl->msg) - 1] = '\0';

            gRfStatus = RF_STATUS_TX;
            pPkt_t->type = PKT_RSP_CTRL;
            pPkt_t->length = PKT_DATA_OFFSET + sizeof(ctrl_rsp_pkt_t);
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start( gTxBuf.TxBuf, 60 );
            return;
        }

        if( gBoundStatus == BOUND_STATUS_EST )
        {
            gTxBuf.len = DATA_LEN_MAX_TX;
            s = UART_RxQuery( (void *)(pPkt_t+1), &gTxBuf.len );
            if( s == 0 )
            {
                gRfStatus = RF_STATUS_TX;
                pPkt_t->type = PKT_DATA_FLAG;
                pPkt_t->length = gTxBuf.len + PKT_DATA_OFFSET;
                gTxBuf.status = STA_BUSY;
                pPkt_t->seq = gTxDataSeq;
                pPkt_t->resv = probe_flow_credit;
                rf_tx_start( gTxBuf.TxBuf, 60 );
                return;
            }
        }
        else
        {
            s = 0x80;
        }

        if (s == 0x80)
        {
            /* A timer-generated status poll is a scheduling hint, not a
             * reason to defer a due current report. */
            if (probe_send_telemetry_if_due(pPkt_t, SYS_GetSysTickCnt(),
                                            probe_flow_credit)) return;
        }

        if( s == 0x80 )
        {
            if( gBoundStatus )
            {
                gRfStatus = RF_STATUS_GETS;
                pPkt_t->type = PKT_CMD_GET_STATUS;
                pPkt_t->length = PKT_DATA_OFFSET;
                pPkt_t->resv = probe_flow_credit;
            }
            else
            {
                bound_req_t *pReq_t = (bound_req_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

                static uint16_t s_req_cnt = 0;
                if (++s_req_cnt % 50 == 1)
                {
                    dbg_printf("[RF] TX BOUND_REQ #%u (serverData=0x%04X)\r\n",
                               s_req_cnt, gServerData);
                }
                if (s_req_cnt > 150 && gServerData != 0)
                {
                    dbg_printf("[RF] Stale serverData 0x%04X timed out, clearing to 0 for fresh pair\r\n", gServerData);
                    gServerData = 0;
                }

                gRfStatus = RF_STATUS_REQ;
                pPkt_t->type = PKT_CMD_BOUND_REQ;
                pPkt_t->length = PKT_DATA_OFFSET + sizeof(bound_req_t);
                pReq_t->interval = ADV_INTERVAL;
                pReq_t->severData = gServerData;
                gTxDataSeq = 0;
                pPkt_t->resv = 0;
            }
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            rf_tx_start( gTxBuf.TxBuf, 60 );
        }
        else
        {
            uint32_t now = SYS_GetSysTickCnt();
            if (probe_send_telemetry_if_due(pPkt_t, now, probe_flow_credit)) return;

            static uint32_t s_last_poll_tick = 0;
            if (gBoundStatus == BOUND_STATUS_EST && (uint32_t)(now - s_last_poll_tick) >= (GetSysClock() / 200))
            {
                s_last_poll_tick = now;
                if (pRfBuf->data_len == 0)
                {
                    getDataProbe = 1;
                }
            }

            if( getDataProbe )
            {
                /* Keep RF and target UART pipelined.  There is still only one
                 * RF request in flight (gTxBuf.status == STA_BUSY), and every
                 * response is appended in sequence order.  Waiting for the
                 * UART ring to become empty here unnecessarily reduced the
                 * bridge to one RF packet per UART drain interval, which made
                 * high-baud ESP ROM downloads appear to be limited to 115200.
                 * The advertised credit prevents the ring from overflowing;
                 * the UART ISR re-arms getDataProbe as space becomes available. */
                getDataProbe--;
                gRfStatus = RF_STATUS_GETS;
                pPkt_t->type = PKT_CMD_GET_STATUS;
                pPkt_t->length = PKT_DATA_OFFSET;
                gTxBuf.status = STA_BUSY;
                pPkt_t->seq = gTxDataSeq;
                pPkt_t->resv = probe_flow_credit;
                rf_tx_start( gTxBuf.TxBuf, 60 );
            }
        }
    }
    else if( gTxBuf.status == STA_RESEND )
    {
        if( gTxBuf.resendCount )
        {
            gRfStatus = RF_STATUS_RETX;
            if( gTxBuf.resendCount != 0xFF ) gTxBuf.resendCount--;
            gTxBuf.status = STA_BUSY;
            rf_tx_start( gTxBuf.TxBuf, 60 );
        }
        else
        {
            gTxBuf.status = STA_IDLE;
        }
    }
}

void RF_UartTxInit( void )
{
    rfBoundInfo_t *pInfo;
    gTxDataSeq = 0;
    gRfRxFlag = 0;
    gBoundStatus = BOUND_STATUS_IDLE;
    gTxBuf.status = 0;
    rf_buffer_create(&pRfBuf);

    pInfo = (rfBoundInfo_t *)(BOUND_INFO_FLASH_ADDR);
    if( pInfo->head == BOUND_INFO_HEAD )
    {
        gServerData = pInfo->serverData;
    }
    else
    {
        gServerData = 0;
    }
    RFRole_RegisterStatusCbs( &rfCBs );
    BLE_AdvInit( s_ble_adv_addr );
}

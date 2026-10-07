/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_uart_rx.c
 * Author             : WCH / Modified for Wireless Current Sensor Tool
 * Version            : V2.0
 * Date               : 2026/09/23
 * Description        : Dongle RF receiver & router for Dual CDC
 *******************************************************************************/

#include "rf.h"
#include "rf_uart_rx.h"
#include "usb_uart.h"
#include "CH57x_pwm.h"
#include <stdio.h>
#include <string.h>

/* Smaller host-to-target RF payloads reduce retransmission probability while
 * still keeping the 115200-baud UART continuously fed. */
#define HOST_DATA_RF_CHUNK_MAX 251u

uint8_t volatile RF_bound_Flag;

/* RF RX DMA buffer defined in rf.c; hardware appends an RSSI byte after each packet. */
extern uint8_t RxBuf[];

rfTxBuf_t gTxBuf;
uint16_t gServerData;
uint16_t gTimeoutMax;
uint16_t gTimeout;

uint8_t gDataSeq;
uint8_t gRfStatus;
uint8_t gBoundStatus;

/* A GET_STATUS response consumes bytes from the USB FIFO.  If the RF
 * response is lost, Probe retries the same request; serving the next FIFO
 * bytes would silently remove a chunk from the ESP ROM stream. */
static uint8_t s_status_rsp_valid = 0;
static uint8_t s_status_rsp_seq = 0;
static uint8_t s_status_rsp_retries = 0;

/* Local accounting for the Probe-generated RF throughput test. */
static uint8_t s_rf_test_active = 0;
static uint32_t s_rf_test_start_tick = 0;
static uint32_t s_rf_test_bytes = 0;

void RF_TestNotifyStart(void)
{
    s_rf_test_active = 1;
    s_rf_test_start_tick = 0;
    s_rf_test_bytes = 0;
}

/* The RF callback only snapshots telemetry.  Formatting and USB writes run
 * in the main loop, keeping this relatively large path out of RAM code. */
static telemetry_pkt_t s_telemetry_snapshot;
static volatile uint8_t s_telemetry_pending;
static volatile uint8_t s_rssi_report_pending;
static volatile int8_t s_telemetry_rssi;
static uint8_t s_rssi_div;

static void rfProcessRx( rfPackage_t *pPkt );
static void rfProcessTx( void );
static void rfProcessCrcError( void );
static void rfProcessTimeout( void );

rfStatusCBs_t rfCBs =
{
    rfProcessRx,
    rfProcessTx,
    rfProcessCrcError,
    rfProcessTimeout,
};

__HIGH_CODE
static uint32_t rf_rand16( uint32_t seed )
{
    static uint32_t holdrand;
    uint64_t tmp;

    holdrand += seed;
    holdrand = holdrand * 1664525 + 1013904223;
    tmp = holdrand;
    tmp = (tmp * 0xFFFF) >> 32;
    return (uint32_t)tmp;
}

__HIGH_CODE
static uint32_t rf_rand_aa( uint16_t rand )
{
    uint32_t aa = rand;
    aa = (aa << 8) | 0x6E0000B6;
    return aa;
}

static void rf_disconnect( void )
{
    RF_bound_Flag = 0;
    s_status_rsp_valid = 0;
    s_status_rsp_retries = 0;
    s_rf_test_active = 0;
    s_rf_test_start_tick = 0;
    s_rf_test_bytes = 0;
    /* Do not call RFRole_Shut(); keep RF active to listen for reconnect on Ch 17 */
    gBoundStatus = BOUND_STATUS_IDLE;
    gServerData = 0;

    rf_tx_set_sync_word( AA );
    rf_tx_set_frequency( DEF_FREQUENCY );
    rf_rx_set_sync_word( AA );
    rf_rx_set_frequency( DEF_FREQUENCY );
    gRfStatus = RF_STATUS_WAIT;
    rf_rx_start( 0 );

    COM2_SendBytes("[LINK] Disconnected, listening on Ch 17\r\n",
                   sizeof("[LINK] Disconnected, listening on Ch 17\r\n") - 1);
}

static void rf_bound( bound_rsp_t *rsp )
{
    char msg[64];
    int ml;
    gTimeout = 0;
    gBoundStatus = BOUND_STATUS_WAIT;
    gServerData = rsp->severData;
    rf_tx_set_sync_word( rsp->accessaddr );
    rf_tx_set_frequency( rsp->channel );
    rf_tx_set_phy_type( rsp->phy );

    rf_rx_set_sync_word( rsp->accessaddr );
    rf_rx_set_frequency( rsp->channel );
    rf_rx_set_phy_type( rsp->phy );
    RF_bound_Flag = 1;

    ml = sprintf(msg, "[LINK] Bound! Ch=%d, Addr=0x%08lX\r\n",
                 (int)rsp->channel, (unsigned long)rsp->accessaddr);
    COM2_SendBytes(msg, (uint16_t)ml);
}

__HIGH_CODE
static void rfProcessRx( rfPackage_t *pPkt )
{
    if (USB_FirmwareUpdateActive())
    {
        /* FW_DATA owns COM2 and Flash while it is active.  Keep the RF
         * receiver parked without forwarding bytes or formatting telemetry. */
        gRfStatus = RF_STATUS_WAIT;
        rf_rx_start(0);
        return;
    }
    if (!pPkt || pPkt->length < PKT_DATA_OFFSET || pPkt->length > DATA_LEN_MAX_RX)
    {
        gRfStatus = RF_STATUS_WAIT;
        rf_rx_start((gBoundStatus == BOUND_STATUS_IDLE) ? 0 : 20000);
        return;
    }

    uint8_t min_len = PKT_DATA_OFFSET;
    if (pPkt->type == PKT_DATA_TELEMETRY)
        min_len = PKT_DATA_OFFSET + sizeof(telemetry_pkt_t);
    else if (pPkt->type == PKT_RSP_CTRL)
        min_len = PKT_DATA_OFFSET + sizeof(ctrl_rsp_pkt_t);
    else if (pPkt->type == PKT_RSP_OTA)
        min_len = PKT_DATA_OFFSET + sizeof(ota_rsp_pkt_t);
    if (pPkt->length < min_len)
    {
        gRfStatus = RF_STATUS_WAIT;
        rf_rx_start((gBoundStatus == BOUND_STATUS_IDLE) ? 0 : 20000);
        return;
    }

    rfPackage_t *pPkt_t = (rfPackage_t *)gTxBuf.TxBuf;

    if (gBoundStatus == BOUND_STATUS_EST &&
        pPkt->type == PKT_CMD_GET_STATUS &&
        s_status_rsp_valid && pPkt->seq == s_status_rsp_seq &&
        s_status_rsp_retries)
    {
        s_status_rsp_retries--;
        gRfStatus = RF_STATUS_TX;
        rf_tx_start(pPkt_t, 60);
        return;
    }

    /* A retransmitted Probe packet must not be forwarded to the target UART
     * twice.  The RF layer can deliver a duplicate when the previous ACK was
     * lost; duplicate SLIP bytes are fatal to ESP ROM download framing. */
    if (gBoundStatus != BOUND_STATUS_IDLE &&
        pPkt->seq == (uint8_t)(gDataSeq - 1u) &&
        (pPkt->type == PKT_DATA_FLAG || pPkt->type == PKT_DATA_TELEMETRY))
    {
        gRfStatus = RF_STATUS_TX;
        pPkt_t->type = PKT_DATA_RSP_ACK;
        pPkt_t->length = PKT_DATA_OFFSET;
        pPkt_t->seq = pPkt->seq;
        pPkt_t->resv = 0;
        rf_tx_start(pPkt_t, 60);
        return;
    }

    if (gBoundStatus == BOUND_STATUS_IDLE)
    {
        if (pPkt->type == PKT_CMD_BOUND_REQ || pPkt->type == PKT_DATA_FLAG || pPkt->type == PKT_CMD_GET_STATUS)
        {
            int8_t rssi = *(uint8_t *)((uint8_t *)pPkt + pPkt->length + 2 + 2);
            bound_rsp_t *pRsp_t = (bound_rsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

            gRfStatus = RF_STATUS_TXRSP;
            gDataSeq = 0;
            gServerData = (uint16_t)rf_rand16((uint32_t)rssi);
            if (gServerData == 0) gServerData = 0x1234;
            pPkt_t->type = PKT_CMD_BOUND_RSP;
            pPkt_t->length = PKT_DATA_OFFSET + sizeof(bound_rsp_t);
            pPkt_t->seq = gDataSeq;
            pPkt_t->resv = 0;
            pRsp_t->accessaddr = rf_rand_aa( gServerData );
            pRsp_t->channel = (uint8_t)(gServerData % 37);
            pRsp_t->phy = CONN_PHY_TYPE;
            pRsp_t->severData = gServerData;
            pRsp_t->interval = CONN_INTERVAL;
            pRsp_t->timeout = CONN_TIMEOUT;
            rf_tx_start( pPkt_t, 60 );
            gDataSeq++;
            gTimeoutMax = BOUND_EST_COUNT;
            return;
        }
        gRfStatus = RF_STATUS_WAIT;
        rf_rx_start( 0 );
    }
    else
    {
        rfRsp_t *pRsp_t = (rfRsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

        if (pPkt->type == PKT_CMD_GET_STATUS)
        {
            gRfStatus = RF_STATUS_TX;
            pPkt_t->seq = pPkt->seq;
            gDataSeq = pPkt->seq + 1;
            pPkt_t->type = PKT_CMD_RSP_STATUS;
            if (gBoundStatus < BOUND_STATUS_EST)
            {
                pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(Uart0Para);
                pRsp_t->opcode = OPCODE_BSP;
                pRsp_t->buad_t.BaudRate = Uart0Para.BaudRate;
                pRsp_t->buad_t.StopBits = Uart0Para.StopBits;
                pRsp_t->buad_t.ParityType = Uart0Para.ParityType;
                pRsp_t->buad_t.DataBits = Uart0Para.DataBits;
                pRsp_t->buad_t.ioStaus = Uart0Para.ioStaus;
                pPkt_t->resv = 0;
            }
            else if (s_probe_ota_pending)
            {
                pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(ota_cmd_pkt_t);
                pRsp_t->opcode = OPCODE_OTA;
                memcpy(pRsp_t->other.rspData, (const void *)&s_probe_ota_cmd, sizeof(ota_cmd_pkt_t));
                pPkt_t->resv = 0;
            }
            else if (COM2_PopPendingCmd(pRsp_t->other.rspData))
            {
                pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(ctrl_cmd_pkt_t);
                pRsp_t->opcode = PKT_CMD_CTRL;
                pPkt_t->resv = 0;
            }
            else
            {
                uint8_t credit = pPkt->resv;
                typeBufSize len = 0;
                if (credit >= 64)
                {
                    len = (credit < HOST_DATA_RF_CHUNK_MAX) ? credit : HOST_DATA_RF_CHUNK_MAX;
                }
                else
                {
                    len = 0; /* Probe buffer cannot safely accept a packet */
                }

                uint8_t s = (len > 0) ? USB_RxQuery( pRsp_t->other.rspData, &len ) : 0xFF;
                if (s == 0)
                {
                    pPkt_t->length = PKT_DATA_OFFSET + 1 + len;
                    pRsp_t->opcode = OPCODE_DATA;
                    pPkt_t->resv = 0;
                    if (len > 0)
                    {
                        g_com1_activity_bytes += len;
                    }
                }
                else if (s == 0x80)
                {
                    pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(Uart0Para);
                    pRsp_t->opcode = OPCODE_BSP;
                    pRsp_t->buad_t.BaudRate = Uart0Para.BaudRate;
                    pRsp_t->buad_t.StopBits = Uart0Para.StopBits;
                    pRsp_t->buad_t.ParityType = Uart0Para.ParityType;
                    pRsp_t->buad_t.DataBits = Uart0Para.DataBits;
                    pRsp_t->buad_t.ioStaus = Uart0Para.ioStaus;
                    pPkt_t->resv = 0;
                }
                else
                {
                    pPkt_t->length = PKT_DATA_OFFSET;
                    pRsp_t->opcode = OPCODE_ACK;
                    pPkt_t->resv = 0;
                }
            }
            rf_tx_start( pPkt_t, 60 );
            if (pPkt_t->type == PKT_CMD_RSP_STATUS)
            {
                s_status_rsp_seq = pPkt->seq;
                s_status_rsp_valid = 1;
                s_status_rsp_retries = 8;
            }
        }
        else if (pPkt->type == PKT_DATA_FLAG)
        {
            gRfStatus = RF_STATUS_TX;
            pPkt_t->seq = pPkt->seq;
            gDataSeq = pPkt->seq + 1;
            typeBufSize len = pPkt->length - PKT_DATA_OFFSET;

            /* RFTEST frames are acknowledged at RF speed but are deliberately
             * kept out of COM1.  The first packet starts the Dongle-side
             * stopwatch, so the result includes only useful RF transfer time. */
            if (s_rf_test_active && len >= 2 &&
                ((const uint8_t *)(pPkt + 1))[0] == RF_TEST_MARK0 &&
                ((const uint8_t *)(pPkt + 1))[1] == RF_TEST_MARK1)
            {
                if (s_rf_test_start_tick == 0) s_rf_test_start_tick = SYS_GetSysTickCnt();
                s_rf_test_bytes += len;
                pPkt_t->type = PKT_DATA_RSP_ACK;
                pPkt_t->length = PKT_DATA_OFFSET;
                pPkt_t->resv = 0;
                rf_tx_start(pPkt_t, 60);
                return;
            }
            COM1_SendBytes((const uint8_t *)(pPkt + 1), len);
            if (len > 0)
            {
                g_com1_activity_bytes += len;
            }
            pPkt_t->type = PKT_DATA_RSP_ACK;
            if (s_probe_ota_pending)
            {
                pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(ota_cmd_pkt_t);
                pRsp_t->opcode = OPCODE_OTA;
                memcpy(pRsp_t->other.rspData, (const void *)&s_probe_ota_cmd, sizeof(ota_cmd_pkt_t));
                pPkt_t->resv = 0;
            }
            else if (COM2_PopPendingCmd(pRsp_t->other.rspData))
            {
                pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(ctrl_cmd_pkt_t);
                pRsp_t->opcode = PKT_CMD_CTRL;
                pPkt_t->resv = 0;
            }
            else
            {
                uint8_t credit = pPkt->resv;
                len = 0;
                if (credit >= 64)
                {
                    len = (credit < DATA_LEN_MAX_TX) ? credit : DATA_LEN_MAX_TX;
                }
                else
                {
                    len = 0; /* Probe buffer full */
                }

                if (len > 0 && gBoundStatus == BOUND_STATUS_EST && !USB_RxQuery(pRsp_t->other.rspData, &len))
                {
                    pPkt_t->length = PKT_DATA_OFFSET + 1 + len;
                    pRsp_t->opcode = OPCODE_DATA;
                    pPkt_t->resv = 0;
                }
                else
                {
                    pPkt_t->length = PKT_DATA_OFFSET;
                    pPkt_t->resv = 0;
                }
            }
            rf_tx_start( pPkt_t, 60 );
        }
        else if (pPkt->type == PKT_DATA_TELEMETRY)
        {
            gRfStatus = RF_STATUS_TX;
            pPkt_t->seq = pPkt->seq;
            gDataSeq = pPkt->seq + 1;
            telemetry_pkt_t *pTelem = (telemetry_pkt_t *)(pPkt + 1);
            s_telemetry_snapshot = *pTelem;
            s_telemetry_pending = 1;
            if (++s_rssi_div >= 20)
            {
                s_rssi_div = 0;
                s_telemetry_rssi = (int8_t)RxBuf[PKT_HEAD_LEN + pPkt->length];
                s_rssi_report_pending = 1;
            }

            pPkt_t->type = PKT_DATA_RSP_ACK;
            if (s_probe_ota_pending)
            {
                pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(ota_cmd_pkt_t);
                pRsp_t->opcode = OPCODE_OTA;
                memcpy(pRsp_t->other.rspData, (const void *)&s_probe_ota_cmd, sizeof(ota_cmd_pkt_t));
                pPkt_t->resv = 0;
            }
            else if (COM2_PopPendingCmd(pRsp_t->other.rspData))
            {
                pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(ctrl_cmd_pkt_t);
                pRsp_t->opcode = PKT_CMD_CTRL;
                pPkt_t->resv = 0;
            }
            else
            {
                pPkt_t->length = PKT_DATA_OFFSET;
                pPkt_t->resv = 0;
            }
            rf_tx_start( pPkt_t, 60 );
        }
        else if (pPkt->type == PKT_RSP_CTRL)
        {
            /* Control response from the Probe (e.g. firmware version). */
            gRfStatus = RF_STATUS_TX;
            pPkt_t->seq = pPkt->seq;
            gDataSeq = pPkt->seq + 1;
            ctrl_rsp_pkt_t *pRspCtrl = (ctrl_rsp_pkt_t *)(pPkt + 1);
            if (pRspCtrl->opcode == CTRL_OP_GET_VER)
            {
                char vbuf[64];
                int vl;
                pRspCtrl->msg[sizeof(pRspCtrl->msg) - 1] = '\0';
                vl = sprintf(vbuf, "[VER] Probe:  %s\r\n", pRspCtrl->msg);
                COM2_SendBytes(vbuf, (uint16_t)vl);
            }
            else if (pRspCtrl->opcode == CTRL_OP_PROBE_DFU)
            {
                COM2_SendBytes("[OK] Probe entering ISP; connect its UART to WCHISPTool\r\n",
                               sizeof("[OK] Probe entering ISP; connect its UART to WCHISPTool\r\n") - 1);
            }
            else if (pRspCtrl->opcode == CTRL_OP_RF_TEST)
            {
                uint32_t elapsed_ms = 0;
                uint32_t rate = 0;
                uint32_t now = SYS_GetSysTickCnt();
                if (s_rf_test_start_tick != 0)
                {
                    uint32_t tick_1ms = GetSysClock() / 1000u;
                    if (tick_1ms == 0) tick_1ms = 1;
                    elapsed_ms = (uint32_t)(now - s_rf_test_start_tick) / tick_1ms;
                }
                if (elapsed_ms != 0) rate = (s_rf_test_bytes * 1000u) / elapsed_ms;
                char tbuf[128];
                int tl = sprintf(tbuf, "[RFTEST] bytes=%lu time=%lums rate=%luB/s %s\r\n",
                                 (unsigned long)s_rf_test_bytes,
                                 (unsigned long)elapsed_ms,
                                 (unsigned long)rate,
                                 pRspCtrl->msg);
                COM2_SendBytes(tbuf, (uint16_t)tl);
                s_rf_test_active = 0;
                s_rf_test_start_tick = 0;
                s_rf_test_bytes = 0;
            }

            pPkt_t->type = PKT_DATA_RSP_ACK;
            pPkt_t->length = PKT_DATA_OFFSET;
            pPkt_t->resv = 0;
            rf_tx_start( pPkt_t, 60 );
        }
        else if (pPkt->type == PKT_RSP_OTA)
        {
            gRfStatus = RF_STATUS_TX;
            pPkt_t->seq = pPkt->seq;
            gDataSeq = pPkt->seq + 1;
            ota_rsp_pkt_t *pRspOta = (ota_rsp_pkt_t *)(pPkt + 1);

            if (s_probe_ota_pending && pRspOta->ota_op == s_probe_ota_cmd.ota_op && pRspOta->chunk_idx == s_probe_ota_cmd.chunk_idx)
            {
                s_probe_ota_rsp = *pRspOta;
                s_probe_ota_pending = 0;
                s_probe_ota_acked = 1;
            }

            pPkt_t->type = PKT_DATA_RSP_ACK;
            pPkt_t->length = PKT_DATA_OFFSET;
            pPkt_t->resv = 0;
            rf_tx_start( pPkt_t, 60 );
        }
        else
        {
            if (++gTimeout > gTimeoutMax)
            {
            }
            else
            {
                gRfStatus = RF_STATUS_WAIT;
            }
            return;
        }

        if (gBoundStatus == BOUND_STATUS_WAIT)
        {
            gBoundStatus = BOUND_STATUS_EST;
            gTimeoutMax = (CONN_TIMEOUT * 10 / CONN_INTERVAL);
        }
        gTimeout = 0;
    }
}

void RF_ProcessTelemetry(void)
{
    telemetry_pkt_t sample;
    uint8_t report_rssi;
    int8_t rssi;
    uint32_t irqv;

    if (!s_telemetry_pending) return;
    SYS_DisableAllIrq(&irqv);
    sample = s_telemetry_snapshot;
    s_telemetry_pending = 0;
    report_rssi = s_rssi_report_pending;
    rssi = s_telemetry_rssi;
    s_rssi_report_pending = 0;
    SYS_RecoverIrq(irqv);

    g_current_report_tick = SYS_GetSysTickCnt();
    /* Reconstruct engineering values from the raw INA226 registers.  This
     * keeps custom shunt settings correct and avoids transmitting duplicate
     * rounded values in every RF telemetry packet. */
    uint32_t shunt_uohm = g_dongle_cfg.shunt_uohm ? g_dongle_cfg.shunt_uohm : 20000u;
    int64_t current_num = (int64_t)sample.shunt_raw * 2500000LL;
    int32_t cur_ua;
    if (current_num >= 0)
        cur_ua = (int32_t)((current_num + shunt_uohm / 2u) / shunt_uohm);
    else
        cur_ua = (int32_t)((current_num - (int64_t)(shunt_uohm / 2u)) / shunt_uohm);
    int32_t cur_ma_int = cur_ua / 1000;
    int32_t cur_ma_frac = cur_ua % 1000;
    if (cur_ma_frac < 0) cur_ma_frac = -cur_ma_frac;

    int32_t abs_cur_ua = (cur_ua < 0) ? -cur_ua : cur_ua;
    if (g_dongle_cfg.full_scale_ma > 0)
    {
        uint64_t duty64 = ((uint64_t)abs_cur_ua * 65535u) /
                          ((uint32_t)g_dongle_cfg.full_scale_ma * 1000u);
        uint32_t duty = (duty64 > 65535u) ? 65535u : (uint32_t)duty64;
        g_current_duty = (uint16_t)duty;
    }

    char tbuf[80];
    uint32_t bus_mv = ((uint32_t)sample.bus_raw * 5u + 2u) / 4u;
    int bus_v = bus_mv / 1000;
    int bus_frac = bus_mv % 1000;
    int64_t pwr_uw = ((int64_t)bus_mv * abs_cur_ua) / 1000;
    int32_t pwr_mw_int = (int32_t)(pwr_uw / 1000);
    int32_t pwr_mw_frac = (int32_t)((pwr_uw % 1000) / 10);
    int l = sprintf(tbuf, "[INA] V:%d.%03dV, I:%d.%03dmA, P:%d.%02dmW\r\n",
                    bus_v, bus_frac, cur_ma_int, cur_ma_frac,
                    pwr_mw_int, pwr_mw_frac);
    COM2_SendBytes(tbuf, (uint16_t)l);

    /* The RF receiver already samples RSSI every 20 telemetry packets.  Do
     * not divide that event again: at a 100 ms report interval the old code
     * produced one RSSI line roughly every 20 seconds, while the UI expired
     * its connection indicator after only a few seconds. */
    if (report_rssi)
    {
        char rssi_buf[32];
        if (rssi >= 0) rssi = -68;
        int rl = sprintf(rssi_buf, "[RSSI] %d dBm\r\n", rssi);
        COM2_SendBytes(rssi_buf, (uint16_t)rl);
    }
}

__HIGH_CODE
static void rfProcessTx( void )
{
    if (USB_FirmwareUpdateActive()) return;
    if (gRfStatus == RF_STATUS_TXRSP)
    {
        bound_rsp_t *pRsp_t = (bound_rsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];
        rf_bound( pRsp_t );
        gRfStatus = RF_STATUS_WAIT;
        rf_rx_start( 20000 );
        return;
    }
    rf_rx_start( (gBoundStatus == BOUND_STATUS_IDLE) ? 0 : 20000 );
}

__HIGH_CODE
static void rfProcessCrcError( void )
{
    if (USB_FirmwareUpdateActive()) return;
    rf_rx_start( (gBoundStatus == BOUND_STATUS_IDLE) ? 0 : 20000 );
}

__HIGH_CODE
static void rfProcessTimeout( void )
{
    if (USB_FirmwareUpdateActive()) return;
    gRfStatus = RF_STATUS_WAIT;
    if (gBoundStatus == BOUND_STATUS_WAIT)
    {
        if (++gTimeout > BOUND_EST_COUNT)
        {
            rf_disconnect();
        }
    }
    else if (gBoundStatus == BOUND_STATUS_EST)
    {
        if (++gTimeout > gTimeoutMax)
        {
            rf_disconnect();
        }
    }
    rf_rx_start( (gBoundStatus == BOUND_STATUS_IDLE) ? 0 : 20000 );
}

void RF_UartRxInit( void )
{
    gBoundStatus = BOUND_STATUS_IDLE;
    gServerData = 0;
    RFRole_RegisterStatusCbs( &rfCBs );
    rf_rx_start( 0 );
}

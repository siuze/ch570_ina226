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

uint8_t volatile RF_bound_Flag;

/* RF RX DMA buffer defined in rf.c; hardware appends an RSSI byte after each packet. */
extern uint8_t RxBuf[];

rfTxBuf_t gTxBuf;
uint32_t gRfTxCount;
uint32_t gIntervalTimer;
uint16_t gServerData;
uint16_t gTimeoutMax;
uint16_t gTimeout;

uint8_t gDataSeq;
uint8_t gRfStatus;
uint8_t gBoundStatus;
uint8_t gRxDataStatus;

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
    PRINT("disconnect.\n");
    RFRole_Shut();
    gBoundStatus = BOUND_STATUS_IDLE;
    gRxDataStatus = DATA_STATUS_START;

    rf_tx_set_sync_word( AA );
    rf_tx_set_frequency( DEF_FREQUENCY );
    rf_rx_set_sync_word( AA );
    rf_rx_set_frequency( DEF_FREQUENCY );
    gRfStatus = RF_STATUS_WAIT;
}

static void rf_bound( bound_rsp_t *rsp )
{
    gTimeout = 0;
    gBoundStatus = BOUND_STATUS_WAIT;
    rf_tx_set_sync_word( rsp->accessaddr );
    rf_tx_set_frequency( rsp->channel );
    rf_tx_set_phy_type( rsp->phy );

    rf_rx_set_sync_word( rsp->accessaddr );
    rf_rx_set_frequency( rsp->channel );
    rf_rx_set_phy_type( rsp->phy );
    RF_bound_Flag = 1;
    PRINT("bound success.%X %x\n", rsp->accessaddr, rsp->channel);
}

__HIGH_CODE
static void rfProcessRx( rfPackage_t *pPkt )
{
    rfPackage_t *pPkt_t = (rfPackage_t *)gTxBuf.TxBuf;

    if (gBoundStatus == BOUND_STATUS_IDLE)
    {
        if (pPkt->type == PKT_CMD_BOUND_REQ)
        {
            BOOL reg = 0;
            bound_req_t *pReq_t = (bound_req_t *)(pPkt + 1);
            if (pPkt->length == sizeof(bound_req_t) + 2)
            {
                int8_t rssi = *(uint8_t *)((uint8_t *)pPkt + pPkt->length + 2 + 2);

                if (pReq_t->severData)
                {
                    if (!gServerData || pReq_t->severData == gServerData)
                    {
                        reg = 1;
                    }
                }
                else
                {
                    if (rssi > -35)
                    {
                        reg = 1;
                    }
                }

                if (reg)
                {
                    bound_rsp_t *pRsp_t = (bound_rsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

                    gRfStatus = RF_STATUS_TXRSP;
                    gDataSeq = 0;
                    gServerData = (uint16_t)rf_rand16((uint32_t)rssi);
                    pPkt_t->type = PKT_CMD_BOUND_RSP;
                    pPkt_t->length = PKT_DATA_OFFSET + sizeof(bound_rsp_t);
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                    pRsp_t->accessaddr = rf_rand_aa( gServerData );
                    pRsp_t->channel = gServerData & 0x3F;
                    pRsp_t->phy = CONN_PHY_TYPE;
                    pRsp_t->severData = gServerData;
                    pRsp_t->interval = CONN_INTERVAL;
                    pRsp_t->timeout = CONN_TIMEOUT;
                    rf_tx_start( pPkt_t, 20 );
                    gDataSeq++;
                    gIntervalTimer = pReq_t->interval * 1000;
                    gTimeoutMax = BOUND_EST_COUNT;
                    return;
                }
            }
        }
        gRfStatus = RF_STATUS_WAIT;
    }
    else
    {
        rfRsp_t *pRsp_t = (rfRsp_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

        if (pPkt->type == PKT_CMD_GET_STATUS)
        {
            gRfStatus = RF_STATUS_TX;
            if (pPkt->seq == gDataSeq)
            {
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
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                else if (COM2_HasPendingCmd())
                {
                    pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(ctrl_cmd_pkt_t);
                    pRsp_t->opcode = PKT_CMD_CTRL;
                    memcpy(pRsp_t->other.rspData, COM2_GetPendingCmd(), sizeof(ctrl_cmd_pkt_t));
                    COM2_ClearPendingCmd();
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                else
                {
                    typeBufSize len = DATA_LEN_MAX_TX;
                    uint8_t s = USB_RxQuery( pRsp_t->other.rspData, &len );
                    if (s == 0)
                    {
                        pPkt_t->length = PKT_DATA_OFFSET + 1 + len;
                        pRsp_t->opcode = OPCODE_DATA;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                        gRfTxCount += len;
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
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                    }
                    else
                    {
                        pPkt_t->length = PKT_DATA_OFFSET;
                        pRsp_t->opcode = OPCODE_ACK;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                    }
                }
                gDataSeq++;
            }
            rf_tx_start( pPkt_t, 20 );
        }
        else if (pPkt->type == PKT_DATA_FLAG)
        {
            gRfStatus = RF_STATUS_TX;
            if (pPkt->seq == gDataSeq)
            {
                typeBufSize len = pPkt->length - PKT_DATA_OFFSET;
                COM1_SendBytes((const uint8_t *)(pPkt + 1), len);
                gRxDataStatus = DATA_STATUS_RCV;

                pPkt_t->type = PKT_DATA_RSP_ACK;
                if (COM2_HasPendingCmd())
                {
                    pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(ctrl_cmd_pkt_t);
                    pRsp_t->opcode = PKT_CMD_CTRL;
                    memcpy(pRsp_t->other.rspData, COM2_GetPendingCmd(), sizeof(ctrl_cmd_pkt_t));
                    COM2_ClearPendingCmd();
                    pPkt_t->seq = gDataSeq;
                    pPkt_t->resv = 0;
                }
                else
                {
                    len = DATA_LEN_MAX_TX;
                    if (gBoundStatus == BOUND_STATUS_EST && !USB_RxQuery(pRsp_t->other.rspData, &len))
                    {
                        pPkt_t->length = PKT_DATA_OFFSET + 1 + len;
                        pRsp_t->opcode = OPCODE_DATA;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                        gRfTxCount += len;
                    }
                    else
                    {
                        pPkt_t->length = PKT_DATA_OFFSET;
                        pPkt_t->seq = gDataSeq;
                        pPkt_t->resv = 0;
                    }
                }
                gDataSeq++;
            }
            rf_tx_start( pPkt_t, 20 );
        }
        else if (pPkt->type == PKT_DATA_TELEMETRY)
        {
            gRfStatus = RF_STATUS_TX;
            if (pPkt->seq == gDataSeq)
            {
                telemetry_pkt_t *pTelem = (telemetry_pkt_t *)(pPkt + 1);

                g_telemetry_in_5s = 1;

                /* Calculate high-resolution current using shunt_raw (1 LSB = 0.125mA = 125uA) */
                int32_t cur_ua = (int32_t)pTelem->shunt_raw * 125;
                int32_t cur_ma_int = cur_ua / 1000;
                int32_t cur_ma_frac = cur_ua % 1000;
                if (cur_ma_frac < 0) cur_ma_frac = -cur_ma_frac;

                int32_t abs_cur_ua = (cur_ua < 0) ? -cur_ua : cur_ua;
                if (g_dongle_cfg.full_scale_ma > 0)
                {
                    uint32_t duty = ((uint32_t)(abs_cur_ua / 1000) * 255) / g_dongle_cfg.full_scale_ma;
                    if (duty > 255) duty = 255;
                    g_current_duty = (uint8_t)duty;
                    PWM2_ActDataWidth(g_current_duty);
                }

                char tbuf[80];
                int bus_v = pTelem->bus_mv / 1000;
                int bus_frac = pTelem->bus_mv % 1000;

                /* High resolution power: P = V(mV) * I(uA) / 1000 = uW */
                int64_t pwr_uw = ((int64_t)pTelem->bus_mv * abs_cur_ua) / 1000;
                int32_t pwr_mw_int = (int32_t)(pwr_uw / 1000);
                int32_t pwr_mw_frac = (int32_t)((pwr_uw % 1000) / 10); // 2 decimal places

                int l = sprintf(tbuf, "V:%d.%03dV, I:%d.%03dmA, P:%d.%02dmW\r\n",
                                bus_v, bus_frac, cur_ma_int, cur_ma_frac,
                                pwr_mw_int, pwr_mw_frac);
                COM2_SendBytes(tbuf, (uint16_t)l);

                /* 1Hz (every 20 packets at 20Hz) 2.4G RF Signal RSSI Report to Host COM2 */
                static uint8_t s_rssi_div = 0;
                if (++s_rssi_div >= 20)
                {
                    s_rssi_div = 0;
                    /* In CH57x RF, hardware appends RSSI byte at end of packet */
                    int8_t rx_rssi = (int8_t)RxBuf[PKT_HEAD_LEN + pPkt->length];
                    if (rx_rssi == 0 || rx_rssi > 0) rx_rssi = -68; // Nominal calibrated value
                    char rssi_buf[32];
                    int rl = sprintf(rssi_buf, "RSSI:%d dBm\r\n", rx_rssi);
                    COM2_SendBytes(rssi_buf, (uint16_t)rl);
                }

                pPkt_t->type = PKT_DATA_RSP_ACK;
                if (COM2_HasPendingCmd())
                {
                    pPkt_t->length = PKT_DATA_OFFSET + 1 + sizeof(ctrl_cmd_pkt_t);
                    pRsp_t->opcode = PKT_CMD_CTRL;
                    memcpy(pRsp_t->other.rspData, COM2_GetPendingCmd(), sizeof(ctrl_cmd_pkt_t));
                    COM2_ClearPendingCmd();
                }
                else
                {
                    pPkt_t->length = PKT_DATA_OFFSET;
                }
                pPkt_t->seq = gDataSeq;
                pPkt_t->resv = 0;
                gDataSeq++;
            }
            rf_tx_start( pPkt_t, 20 );
        }
        else if (pPkt->type == PKT_RSP_CTRL)
        {
            /* Control response from the Probe (e.g. firmware version). */
            gRfStatus = RF_STATUS_TX;
            if (pPkt->seq == gDataSeq)
            {
                ctrl_rsp_pkt_t *pRspCtrl = (ctrl_rsp_pkt_t *)(pPkt + 1);
                if (pRspCtrl->opcode == CTRL_OP_GET_VER)
                {
                    char vbuf[64];
                    int vl;
                    pRspCtrl->msg[sizeof(pRspCtrl->msg) - 1] = '\0';
                    vl = sprintf(vbuf, "[VER] Probe:  %s\r\n", pRspCtrl->msg);
                    COM2_SendBytes(vbuf, (uint16_t)vl);
                }

                pPkt_t->type = PKT_DATA_RSP_ACK;
                pPkt_t->length = PKT_DATA_OFFSET;
                pPkt_t->seq = gDataSeq;
                pPkt_t->resv = 0;
                gDataSeq++;
            }
            rf_tx_start( pPkt_t, 20 );
        }
        else
        {
            if (++gTimeout > gTimeoutMax)
            {
                gRxDataStatus = DATA_STATUS_TIMEOUT;
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
            gIntervalTimer = CONN_INTERVAL * 1000;
            gTimeoutMax = (CONN_TIMEOUT * 10 / CONN_INTERVAL);
        }
        gTimeout = 0;
    }
}

__HIGH_CODE
static void rfProcessTx( void )
{
    rf_rx_start( 0 );
}

__HIGH_CODE
static void rfProcessCrcError( void )
{
    rf_rx_start( 0 );
}

__HIGH_CODE
static void rfProcessTimeout( void )
{
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
    rf_rx_start( 0 );
}

__HIGH_CODE
void RF_UartRxInit( void )
{
    PRINT("----------------- rf uart rx mode -----------------\n");
    gBoundStatus = BOUND_STATUS_IDLE;
    gRxDataStatus = DATA_STATUS_START;
    gServerData = 0;
    gRfTxCount = 0;
    RFRole_RegisterStatusCbs( &rfCBs );
}

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
static const uint8_t s_ble_adv_addr[6] = { 0xC0, 0x11, 0x22, 0x33, 0x44, 0x57 };
static volatile uint8_t s_ver_rsp_pending = 0;   /* set by CTRL_OP_GET_VER, sent by RF_StatusQuery */

static void rfProcessRx( rfPackage_t *pPkt );
static void rfProcessTx( void );
static void rfProcessTimeout( void );

rfStatusCBs_t rfCBs =
{
    rfProcessRx,
    rfProcessTx,
    rfProcessTimeout,
    rfProcessTimeout,
};

#define RF_BUF_LEN 512
static uint8_t rf_buf[RF_BUF_LEN];
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
    }
    else
    {
        g_dev_config.magic = CFG_MAGIC;
        g_dev_config.shunt_uohm = 20000;          // 20 mOhm (matches R104)
        g_dev_config.avg_samples = INA226_AVG_16; // 16 samples
        g_dev_config.report_interval_ms = 100;    // 100ms
        g_dev_config.full_scale_ma = 1000;        // 1000mA
        g_dev_config.uart_swapped = 0;            // Normal
        g_dev_config.resv = 0;
    }
}

void Config_Save(void)
{
    FLASH_ROM_ERASE( CFG_FLASH_ADDR, 4096 );
    FLASH_ROM_WRITE( CFG_FLASH_ADDR, &g_dev_config, sizeof(dev_config_t) );
}

static void rf_buffer_create(struct simple_buf **buf)
{
    *buf = simple_buf_create(&rf_buffer, rf_buf, sizeof(rf_buf) );
}

static void rf_disconnect( void )
{
    RF_bound_Flag = 0;
    PRINT("disconnect.\n" );
    RFRole_Shut( );
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
    PRINT("bound success.%x %x\n",rsp->accessaddr,rsp->channel );
}

static void handle_control_command( ctrl_cmd_pkt_t *pCmd )
{
    if( !pCmd ) return;
    PRINT("Ctrl cmd opcode=%d\n", pCmd->opcode);
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
            INA226_SetConfig( g_dev_config.shunt_uohm, g_dev_config.avg_samples );
            Config_Save();
            break;
        case CTRL_OP_GET_VER:
            /* Stage a firmware-version response; RF_StatusQuery transmits it. */
            s_ver_rsp_pending = 1;
            break;
        default:
            break;
    }
}

__HIGH_CODE
static void rfProcessRx( rfPackage_t *pPkt )
{
    if( gTxDataSeq == pPkt->seq )
    {
        if( pPkt->type == PKT_DATA_RSP_ACK )
        {
            if( pPkt->length > PKT_DATA_OFFSET+1 )
            {
                typeBufSize len;
                pPkt_t = pPkt;
                rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt_t+1);

                if( pRsp_t->opcode == PKT_CMD_CTRL )
                {
                    handle_control_command( (ctrl_cmd_pkt_t *)(pRsp_t->other.rspData) );
                }
                else
                {
                    len = pPkt_t->length-PKT_DATA_OFFSET-1;
                    gRfRxFlag = write_buf( pRfBuf, pRsp_t->other.rspData, &len );
                    if( !len )
                    {
                        UART_Send( (char *)pRsp_t->other.rspData, pPkt_t->length-PKT_DATA_OFFSET-1 );
                    }
                    if( !R8_UART_TFC )
                    {
                        PFIC_SetPendingIRQ( UART_IRQn );
                    }
                }
            }
            getDataProbe = 6;
        }
        else if( pPkt->type == PKT_CMD_BOUND_RSP )
        {
            rf_bound( (bound_rsp_t *)(pPkt+1) );
        }
        else if( pPkt->type == PKT_CMD_CTRL )
        {
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
            else if( pRsp_t->opcode == PKT_CMD_CTRL )
            {
                handle_control_command( (ctrl_cmd_pkt_t *)(pRsp_t->other.rspData) );
            }
            else if( pRsp_t->opcode == OPCODE_BSP )
            {
                if((pRsp_t->buad_t.BaudRate > 400000) && (pRsp_t->buad_t.BaudRate < 1000000))
                {
                    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);
                }
                else
                {
                    SetSysClock(CLK_SOURCE_HSE_PLL_24MHz);
                }
                mDelaymS(10);

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
            else if( pRsp_t->opcode == OPCODE_DATA )
            {
                typeBufSize len;
                pPkt_t = pPkt;
                getDataProbe = 6;

                rfRsp_t *pRsp_t = (rfRsp_t *)(pPkt_t+1);
                len = pPkt_t->length-PKT_DATA_OFFSET-1;
                gRfRxFlag = write_buf( pRfBuf, pRsp_t->other.rspData, &len );
                if( !len )
                {
                    UART_Send( (char *)pRsp_t->other.rspData, pPkt_t->length-PKT_DATA_OFFSET-1 );
                }
                if( !R8_UART_TFC )
                {
                    PFIC_SetPendingIRQ( UART_IRQn );
                }
            }
        }

        gRfStatus = RF_STATUS_IDLE;
        gTxBuf.status = STA_IDLE;
        gTxDataSeq++;
        gTimeout = 0;
    }
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
    rf_rx_start( 150 );
}

__HIGH_CODE
static void rfProcessTimeout( void )
{
    if( BLE_AdvActive() )
    {
        BLE_AdvMarkDone();
        return;
    }
    gTxBuf.status = STA_RESEND;
    if( gRfStatus == RF_STATUS_WAITRSP )
    {
        gTxBuf.resendCount = RESEND_COUNT;
    }
    else
    {
        if( gBoundStatus == BOUND_STATUS_WAIT )
        {
            if( ++gTimeout > BOUND_EST_COUNT )
            {
                rf_disconnect();
            }
            gTxBuf.status = STA_IDLE;
        }
        else if( gBoundStatus == BOUND_STATUS_EST )
        {
            if( ++gTimeout > gTimeoutMax )
            {
                rf_disconnect();
                gTxBuf.status = STA_IDLE;
            }
        }
    }
}

__HIGH_CODE
void RF_StatusQuery( void )
{
    uint8_t s;

    if( gTxBuf.status == STA_IDLE )
    {
        /* Degraded mode: not bound to the dongle -> beacon telemetry over BLE.
         * Still fall through to the bound-request path between bursts so the
         * probe rejoins the dongle as soon as it appears. */
        if( !gBoundStatus )
        {
            uint32_t now = SYS_GetSysTickCnt();
            uint32_t tick_1ms = GetSysClock() / 1000;
            if( s_unbound_since_tick == 0 ) s_unbound_since_tick = now;
            if( ((now - s_unbound_since_tick) >= (BLE_ADV_DEGRADE_MS * tick_1ms)) &&
                ((now - s_last_adv_tick)      >= (BLE_ADV_PERIOD_MS  * tick_1ms)) )
            {
                ina226_data_t idata;
                s_last_adv_tick = now;
                if( INA226_ReadData(&idata) )
                {
                    BLE_AdvSetTelemetryRaw( idata.shunt_raw, idata.bus_raw );
                }
                BLE_AdvBurst();
                return;
            }
        }
        else
        {
            s_unbound_since_tick = 0;
        }

        rfPackage_t *pPkt_t = (rfPackage_t *)gTxBuf.TxBuf;

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

        gTxBuf.len = DATA_LEN_MAX_TX;
        s = UART_RxQuery( (void *)(pPkt_t+1), &gTxBuf.len );
        if( s == 0 )
        {
            gRfStatus = RF_STATUS_TX;
            pPkt_t->type = PKT_DATA_FLAG;
            pPkt_t->length = gTxBuf.len + PKT_DATA_OFFSET;
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start( gTxBuf.TxBuf, 60 );
        }
        else if( s == 0x80 )
        {
            if( gBoundStatus )
            {
                gRfStatus = RF_STATUS_GETS;
                pPkt_t->type = PKT_CMD_GET_STATUS;
                pPkt_t->length = PKT_DATA_OFFSET;
            }
            else
            {
                bound_req_t *pReq_t = (bound_req_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];

                gRfStatus = RF_STATUS_REQ;
                pPkt_t->type = PKT_CMD_BOUND_REQ;
                pPkt_t->length = PKT_DATA_OFFSET + sizeof(bound_req_t);
                pReq_t->interval = ADV_INTERVAL;
                pReq_t->severData = gServerData;
                gTxDataSeq = 0;
            }
            gTxBuf.status = STA_BUSY;
            pPkt_t->seq = gTxDataSeq;
            pPkt_t->resv = 0;
            rf_tx_start( gTxBuf.TxBuf, 60 );
        }
        else
        {
            uint32_t now = SYS_GetSysTickCnt();
            uint32_t interval_ticks = g_dev_config.report_interval_ms * (GetSysClock() / 1000);
            if( gBoundStatus == BOUND_STATUS_EST && ((now - s_last_telemetry_tick) >= interval_ticks) )
            {
                ina226_data_t idata;
                s_last_telemetry_tick = now;
                if( INA226_ReadData(&idata) )
                {
                    telemetry_pkt_t *pTelem = (telemetry_pkt_t *)&gTxBuf.TxBuf[PKT_HEAD_LEN];
                    pTelem->current_ma = idata.current_ma;
                    pTelem->bus_mv = (uint16_t)idata.bus_mv;
                    pTelem->power_mw = (uint16_t)idata.power_mw;
                    pTelem->shunt_raw = idata.shunt_raw;
                    pTelem->flags = 0;

                    gRfStatus = RF_STATUS_TX;
                    pPkt_t->type = PKT_DATA_TELEMETRY;
                    pPkt_t->length = PKT_DATA_OFFSET + sizeof(telemetry_pkt_t);
                    gTxBuf.status = STA_BUSY;
                    pPkt_t->seq = gTxDataSeq;
                    pPkt_t->resv = 0;
                    rf_tx_start( gTxBuf.TxBuf, 60 );
                    return;
                }
            }

            if( getDataProbe )
            {
                getDataProbe--;
                gRfStatus = RF_STATUS_GETS;
                pPkt_t->type = PKT_CMD_GET_STATUS;
                pPkt_t->length = PKT_DATA_OFFSET;
                gTxBuf.status = STA_BUSY;
                pPkt_t->seq = gTxDataSeq;
                pPkt_t->resv = 0;
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
            PRINT(" resend fail.%d\n", gTxBuf.TxBuf[1]);
            gTxBuf.status = STA_IDLE;
        }
    }
}

__HIGH_CODE
void RF_UartTxInit( void )
{
    rfBoundInfo_t *pInfo;
    PRINT("----------------- rf uart tx mode -----------------\n");
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
    PRINT("gServerData = %x \n", gServerData);
    RFRole_RegisterStatusCbs( &rfCBs );
    BLE_AdvInit( s_ble_adv_addr );
}

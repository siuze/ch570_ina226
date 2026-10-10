/********************************** (C) COPYRIGHT *******************************
* File Name          : rf_basic.h
* Author             : WCH
* Version            : V1.0
* Date               : 2022/03/10
* Description        : 
*******************************************************************************/

#ifndef __RF_BASIC_H
#define __RF_BASIC_H

#ifdef __cplusplus
extern "C"
{
#endif
#include <CH572rf.h>
#include "CH57x_common.h"
#include "buf.h"

/* Firmware version string reported to the host via "CMD:VER?" (format: YYYY-MM-DD rNN).
 * Override at build time with -DFW_VERSION_STR=\"...\" if desired. */
#ifndef PROBE_WITHOUT_UART
#define PROBE_WITHOUT_UART     0
#endif

#ifndef FW_VERSION_STR
#if PROBE_WITHOUT_UART
#define FW_VERSION_STR         "r22--without-UART-for-test"
#else
#define FW_VERSION_STR         "2026-10-11 r22"
#endif
#endif

/* When enabled by tools/build_firmware.ps1, PA0/PA1 are LED outputs and the
 * target UART passthrough is disabled for the temporary test image. */

#define  DEF_FREQUENCY   17              // ͨ��Ƶ��
#define  TEST_PHY_MODE   PHY_MODE_PHY_2M

#if(TEST_PHY_MODE == PHY_MODE_2G4 )

#if(PHY_2G4_MODE == 0 )
#define   AA           0x94826E8E
#define   AA_EX        0
#define   AA_LEN       1
#define   PRE_LEN      1
#define   CRC_INIT     0xFFFF
#define   CRC_POLY     0x8810
#define   CRC_LEN      2
#define   CTL_FILED    0
#define   DPL_EN       0
#define   DATA_ORDER   0
#define   MODE_2G4     PHY_2G4_1M
#define   CRC_XOR_EN   0

#elif(PHY_2G4_MODE == 1 )
#define PKT_DET_CFG4( var )          { (*((PUINT32V)0x4000C120))= var; } // Demodulation parameter
#define   AA           0x94826E8E
#define   AA_EX        0
#define   AA_LEN       1
#define   PRE_LEN      1
#define   CRC_INIT     0xFFFF
#define   CRC_POLY     0x8810
#define   CRC_LEN      2
#define   CTL_FILED    0
#define   DPL_EN       0
#define   DATA_ORDER   0
#define   MODE_2G4     PHY_2G4_1M
#define   CRC_XOR_EN   0

#elif(PHY_2G4_MODE == 2 )
#define   AA           0x94826E8E
#define   AA_EX        0
#define   AA_LEN       1
#define   PRE_LEN      1
#define   CRC_INIT     0xFFFF
#define   CRC_POLY     0x8810
#define   CRC_LEN      2
#define   CTL_FILED    0
#define   DPL_EN       0
#define   DATA_ORDER   1
#define   MODE_2G4     PHY_2G4_1M
#define   CRC_XOR_EN   1

#endif

#else

#define   AA           0x57250425
#define   AA_EX        0
#define   CRC_INIT     0X555555
#define   CRC_POLY     0x80032d

#endif

/* package type */
typedef struct
{
    uint8_t type;                 //!< package type
    uint8_t length;               //!< data length
    uint8_t seq;                  //!< seq
    uint8_t resv;                 //!< reserved
} rfPackage_t;

/* package type */
#define  PKT_HEAD_LEN          sizeof(rfPackage_t)
#define  PKT_DATA_OFFSET      (PKT_HEAD_LEN-2)

#define  DATA_LEN_MAX_TX    (251)  //!< the max length of rf tx. [251]
#define  BUF_LEN_TX         (DATA_LEN_MAX_TX+PKT_HEAD_LEN)

#define  DATA_LEN_MAX_RX     BUF_LEN_TX  //!< the max length of rf rx.

#define  BOUND_EST_COUNT    6

enum data_status
{
    DATA_STATUS_IDLE,
    DATA_STATUS_START,
    DATA_STATUS_RCV,
    DATA_STATUS_TIMEOUT,
};

enum bound_status
{
    BOUND_STATUS_IDLE,
    BOUND_STATUS_WAIT,
    BOUND_STATUS_EST,
};

/* rf status */
enum rf_status
{
    RF_STATUS_IDLE,
    RF_STATUS_REQ,
    RF_STATUS_GETS,
    RF_STATUS_WAIT,
    RF_STATUS_RX,
    RF_STATUS_TX,
    RF_STATUS_WAITRSP,
    RF_STATUS_TXRSP,
    RF_STATUS_TXACK,
    RF_STATUS_RETX,
    RF_STATUS_REWAIT,

};
typedef struct
{
    uint8_t TxBuf[BUF_LEN_TX];
    typeBufSize len;
    uint8_t status;
    uint8_t resendCount;
} rfTxBuf_t;

typedef struct  __attribute__((packed))
{
    uint16_t interval; // �㲥������ȷ���ڼ䣬���ͼ������λ��ms
    uint16_t severData; // ��������Ϊ�ϴ����ӵ���Ϣ���״�����д0
} bound_req_t;

typedef struct  __attribute__((packed))
{
    uint32_t accessaddr;// ����ͨ�ţ������ַ
    uint8_t channel; // ����ͨ�ţ�ͨ��Ƶ��
    uint8_t phy;     // ����ͨ�ţ�PHY����
    uint16_t severData; // ����ʶ���������Ϣ
    uint16_t interval; // ����ͨ�ţ�����ʱ���ͻ�ȡ״̬����ļ������λ��ms
    uint16_t timeout;  // ����ͨ�ţ��Ͽ�ʱ�䣬��λ��10ms
} bound_rsp_t;


typedef struct __attribute__((packed))
{
    uint8_t opcode;
    union{

        struct {
            uint32_t  BaudRate;   /* ������ */
            uint8_t StopBits;   /* ֹͣλ������0��1ֹͣλ��1��1.5ֹͣλ��2��2ֹͣλ */
            uint8_t ParityType;   /* У��λ��0��None��1��Odd��2��Even��3��Mark��4��Space */
            uint8_t DataBits;   /* ����λ������5��6��7��8 */
            uint8_t ioStaus;
        } buad_t;

        struct {
            uint8_t rspData[BUF_LEN_TX];
        }other;
    };
} rfRsp_t;

/* pkt type */
#define  PKT_CMD_BOUND_REQ     0x01
#define  PKT_CMD_GET_STATUS    0x02
#define  PKT_DATA_FLAG         0x7E

/* New Packet Types for Current Telemetry & Control */
#define  PKT_CMD_CTRL          0x30
#define  PKT_DATA_TELEMETRY    0x31
#define  PKT_RSP_CTRL          0x32
#define  PKT_RSP_OTA           0x33
#define  OPCODE_OTA            0x40

#define  PROBE_SLOT_B_ADDR     0x00020000
#define  PROBE_MAX_FW_SIZE     (32 * 1024)

#define  OTA_OP_START          0x01
#define  OTA_OP_DATA           0x02
#define  OTA_OP_FINISH         0x03
#define  OTA_OP_ABORT          0x04

#define  OTA_STATUS_OK         0x00
#define  OTA_STATUS_ERR_SIZE   0x01
#define  OTA_STATUS_ERR_ERASE  0x02
#define  OTA_STATUS_ERR_WRITE  0x03
#define  OTA_STATUS_ERR_CRC    0x04
#define  OTA_STATUS_ERR_STATE  0x05

typedef struct __attribute__((packed))
{
    uint8_t  ota_op;
    uint8_t  len;
    uint16_t chunk_idx;
    uint32_t offset;
    uint32_t crc32;
    uint8_t  data[64];
} ota_cmd_pkt_t;

typedef struct __attribute__((packed))
{
    uint8_t  ota_op;
    uint8_t  status;
    uint16_t chunk_idx;
    uint32_t offset;
} ota_rsp_pkt_t;
#define  CFG_FLASH_ADDR        (1024 * 232)
#define  CFG_MAGIC             0x5A5AA5A5

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint32_t shunt_uohm;
    uint16_t avg_samples;
    uint16_t report_interval_ms;
    uint16_t full_scale_ma;
    uint8_t  uart_swapped;
    uint8_t  link_led_max_duty;
    uint8_t  ble_adv_hz;
} dev_config_t;

#define CTRL_OP_RESET          0x01
#define CTRL_OP_BOOT           0x02
#define CTRL_OP_SWAP_UART      0x03
#define CTRL_OP_SET_CFG        0x04
#define CTRL_OP_GET_CFG        0x05
#define CTRL_OP_GET_VER        0x06
#define CTRL_OP_PROBE_DFU      0x07
#define CTRL_OP_RF_TEST        0x08

/* Reserved payload marker for the RF throughput test.  Test frames are
 * consumed by the Dongle and never enter the target UART stream. */
#define RF_TEST_MARK0          0xA5
#define RF_TEST_MARK1          0x5A
#define RF_TEST_PAYLOAD_LEN    128u

typedef struct __attribute__((packed))
{
    uint8_t opcode;
    uint8_t param8;
    uint16_t param16;
    uint32_t param32;
    uint16_t full_scale_ma;
    uint8_t  link_led_max_duty;
    uint8_t  ble_adv_hz;
} ctrl_cmd_pkt_t;

typedef struct __attribute__((packed))
{
    int16_t shunt_raw;
    uint16_t bus_raw;
    uint8_t flags;
} telemetry_pkt_t;

typedef struct __attribute__((packed))
{
    uint8_t opcode;
    uint8_t status;
    uint16_t resv;
    char msg[32];
} ctrl_rsp_pkt_t;

#define  PKT_CMD_BOUND_RSP     (0X80|PKT_CMD_BOUND_REQ)
#define  PKT_CMD_RSP_STATUS    (0X80|PKT_CMD_GET_STATUS)
#define  PKT_DATA_RSP_ACK      (0X80|PKT_DATA_FLAG)

typedef void (*pfnRfRxCB_t)( rfPackage_t *pPkt );
typedef void (*pfnRfTxCB_t)( void );
typedef void (*pfnRfRxCrcCB_t)( void  );
typedef void (*pfnRfTimeoutCB_t)( void );

typedef struct
{
    pfnRfRxCB_t pfnRxCB;
    pfnRfTxCB_t pfnTxCB;
    pfnRfRxCrcCB_t pfnCrcErrCB;
    pfnRfTimeoutCB_t pfnTimeoutCB;
} rfStatusCBs_t;

typedef struct
{
    uint16_t head;
    uint16_t serverData;
} rfBoundInfo_t;

#define  BOUND_INFO_HEAD         0X55AA


/* respond opcode define */
#define  OPCODE_DATA           0x00
#define  OPCODE_BSP            0x01
#define  OPCODE_ACK            0xF0

void rf_tx_set_frequency( uint32_t f );
void rf_tx_set_sync_word( uint32_t sync_word );
void rf_tx_start( void *pBuf, uint16_t rfon_us );
void rf_tx_set_phy_type( uint8_t phy );

void rf_rx_set_sync_word( uint32_t sync_word );
void rf_rx_set_frequency( uint32_t f );
void rf_rx_start( uint32_t rx_us );
void rf_rx_set_phy_type( uint8_t phy );

void RFRole_Init(void);
void RFRole_RegisterStatusCbs( rfStatusCBs_t * p );

#ifdef __cplusplus
}
#endif

#endif

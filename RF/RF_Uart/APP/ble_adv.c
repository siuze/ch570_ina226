/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_adv.c
 * Version            : V1.0
 * Date               : 2026/09/24
 * Description        : Connectionless BLE advertising (beacon fallback) for the
 *                      probe. See ble_adv.h for the rationale and references.
 *
 * Implementation notes
 * --------------------
 * 1. RF configuration verified against the WCH RF_Extend examples / the MIT
 *    reference implementation: PHY_MODE_PHY_1M + PKT_DET_CFG4(0x3f) +
 *    PKT_DET_CFG3(156) + R32_MISC_CTRL bits[29:24]=0x0e. PKT_DET_CFG3 is the
 *    receive sync-acquisition time; leaving it wrong makes TX/RX unstable.
 * 2. In 1M mode the byte 0 handed to the DMA is the PDU header; there is no
 *    extra length prefix.
 * 3. In 1M mode the hardware appends the 3-byte CRC24 using crcInit/crcPoly,
 *    and whitens per whiteChannel. Software must NOT add CRC or placeholder
 *    bytes, and must NOT pre-whiten.
 * 4. The TX wait loop and the DMA buffers live in internal RAM (.highcode);
 *    fetching code from Flash during an RF DMA window is unreliable.
 * 5. The proprietary dongle link and this beacon share one RF IP. A burst
 *    therefore saves the proprietary packet-detection / misc registers,
 *    applies the BLE ones, transmits, then restores them, and raises
 *    g_ble_adv_active so the link state machine ignores the TX interrupt.
 *******************************************************************************/

#include "ble_adv.h"
#include "CH572rf.h"

/*=============================================================================
 * RF register helpers (addresses per CH572 RF IP map)
 *===========================================================================*/
#define REG_PKT_DET_CFG3        (*((volatile uint32_t *)0x4000C11C))
#define REG_PKT_DET_CFG4        (*((volatile uint32_t *)0x4000C120))

/* BLE broadcast access address (spec value, written verbatim, no byte swap) */
#define BLE_ADV_ACCESS_ADDR     0x8E89BED6u
/* BLE CRC24: poly x^24+x^10+x^9+x^6+x^4+x^3+x+1 = 0x00065B, init 0x555555.
 * The hardware register wants the 24-bit reversed polynomial 0x80032D. */
#define HW_CRC_INIT             0x555555u
#define HW_CRC_POLY             0x80032Du
/* Receive sync-acquisition time for 1M PHY + 4-byte access address */
#define PKT_DET_TIMACQ_1M_ADDR4 156

/* PDU header for ADV_NONCONN_IND using a public advertiser address. */
#define PDU_ADV_NONCONN_IND     0x02u

/* TX wait guard, ~5 ms, enough for PLL lock + air time */
#define RF_TX_TIMEOUT_LOOPS     (FREQ_SYS / 1000)

/* PDU header(2) + AdvA(6) + ADV data(31) = 39; round up */
#define RF_TX_BUF_SIZE          48

/*=============================================================================
 * Module state (buffers in internal RAM for the RF DMA)
 *===========================================================================*/
/* TX buffers must live in internal RAM for the RF DMA (.bss is RAM); the code
 * paths run from .highcode. Do NOT place objects and functions in the same
 * section (gcc reports a section type conflict). */
static uint8_t s_tx_dma[RF_TX_BUF_SIZE] __attribute__((aligned(4)));

static uint8_t  s_adv_addr[6];
static uint8_t  s_adv_len;
static uint8_t  s_adv_sequence;
static uint8_t  s_tx_len;

static rfipTx_t s_tx;

static volatile uint8_t s_tx_done;
volatile uint8_t  g_ble_adv_active;

/* Saved proprietary RF register context, restored after each burst */
static uint32_t s_saved_misc;
static uint32_t s_saved_det3;
static uint32_t s_saved_det4;
static uint8_t  s_regs_saved;

/*=============================================================================
 * Integration hooks
 *===========================================================================*/
__HIGH_CODE uint8_t BLE_AdvActive(void)
{
    return g_ble_adv_active;
}

__HIGH_CODE void BLE_AdvMarkDone(void)
{
    s_tx_done = 1;
}

/*=============================================================================
 * High-Resolution Compact BLE Payload
 *===========================================================================*/
/* Compact Service Data (0x16) for UUID 0xFCD2:
 *   AD length = 0x08 (type + UUID + packed current/voltage[4] + sequence)
 *   Byte 1: AD Type = 0x16 (Service Data - 16-bit UUID)
 *   Byte 2..3: UUID = 0xD2, 0xFC (0xFCD2)
 *   Byte 4..7: packed uint32_t: current signed 17-bit (0.125mA/LSB),
 *               bus voltage unsigned 15-bit (1.25mV/LSB)
 *   Byte 8: rolling sequence, used to detect lost advertisements
 */
void BLE_AdvSetTelemetryFixed(int32_t current_ua, uint32_t bus_uv, uint32_t power_mw)
{
    (void)power_mw;
    uint8_t *d = &s_tx_dma[8];
    uint8_t n = 0;

    /* Flags: LE General Discoverable, BR/EDR not supported (3B) */
    d[n++] = 0x02; d[n++] = 0x01; d[n++] = 0x06;

    /* Service Data (0x16) for UUID 0xFCD2 */
    d[n++] = 0x08;                           /* type + UUID + 2B current + 2B voltage + seq */
    d[n++] = 0x16;                           /* AD Type: Service Data - 16-bit UUID */
    d[n++] = 0xD2; d[n++] = 0xFC;             /* Service UUID 0xFCD2 (little endian) */
    /* Signed 17-bit current and 15-bit bus voltage share one 32-bit word. */
    int32_t current_code = (current_ua >= 0) ?
                           (current_ua + 62) / 125 :
                           (current_ua - 62) / 125;
    if (current_code > 0xFFFF) current_code = 0xFFFF;
    if (current_code < -0x10000) current_code = -0x10000;
    uint32_t bus_code = (bus_uv + 625u) / 1250u;
    if (bus_code > 0x7FFFu) bus_code = 0x7FFFu;
    uint32_t packed = ((uint32_t)bus_code << 17) | ((uint32_t)current_code & 0x1FFFFu);
    d[n++] = (uint8_t)packed;
    d[n++] = (uint8_t)(packed >> 8);
    d[n++] = (uint8_t)(packed >> 16);
    d[n++] = (uint8_t)(packed >> 24);
    d[n++] = ++s_adv_sequence;

    /* Complete Local Name (0x09): "CH570" (7B total) */
    const char name[] = "CH570";
    uint8_t nlen = (uint8_t)(sizeof(name) - 1);
    d[n++] = nlen + 1;
    d[n++] = 0x09;
    for (uint8_t k = 0; k < nlen; k++) d[n++] = (uint8_t)name[k];

    s_adv_len = n;
    /* Build directly in the DMA buffer while the RF IP is idle. */
    s_tx_dma[0] = PDU_ADV_NONCONN_IND;
    s_tx_dma[1] = (uint8_t)(6 + s_adv_len);
    for (uint8_t i = 0; i < 6; i++) s_tx_dma[2 + i] = s_adv_addr[i];
    s_tx_len = (uint8_t)(8 + s_adv_len);
}

void BLE_AdvSetTelemetry(int32_t current_ma, uint16_t bus_mv, uint32_t power_mw)
{
    BLE_AdvSetTelemetryFixed(current_ma * 1000, (uint32_t)bus_mv * 1000u, power_mw);
}

/*=============================================================================
 * Init
 *===========================================================================*/
void BLE_AdvInit(const uint8_t addr[6])
{
    TPROPERTIES_CFG props;

    for (uint8_t i = 0; i < 6; i++) s_adv_addr[i] = addr[i];

    /* Default payload so the very first burst is already valid */
    BLE_AdvSetTelemetryFixed(0, 0, 0);

    s_tx.accessAddress   = BLE_ADV_ACCESS_ADDR;
    s_tx.accessAddressEx = 0;
    s_tx.crcInit         = HW_CRC_INIT;
    s_tx.crcPoly         = HW_CRC_POLY;

    props.cfgVal         = 0;
    props.whitOff        = 0;                /* hardware whitening on */
    props.whitChannel    = 1;                /* per-channel whitening index */
    props.modePHY        = PHY_MODE_PHY_1M;  /* 1M PHY */
    props.lengthCrc      = 0;                /* 1M: hardware appends CRC24 */
    props.ctlFiled       = 0;
    props.lengthAA       = 1;                /* 4-byte access address */
    props.lengthPreamble = 1;                /* 1-byte preamble */
    props.dplEnable      = 0;                /* fixed length */
    props.mode2G4        = PHY_2G4_1M;       /* 1 Mbps */
    props.bitOrderData   = 1;                /* LSB first, per BLE spec */
    props.crcXOREnable   = 0;
    /* props.bitOrderData already sets DATA_BIT_ORDER (bit 27).  Bit 28 is
     * reserved and must remain zero. */
    s_tx.properties      = props.cfgVal;

    s_tx.txDMA           = (uint32_t)s_tx_dma;
    s_tx.txLen           = 0;
    s_tx.waitTime        = 80 * 2;           /* PLL lock wait, >= 80us */
    s_tx.txPowerVal      = LL_TX_POWEER_7_DBM;

    g_ble_adv_active = 0;
    s_regs_saved = 0;
}

/*=============================================================================
 * Transmit
 *===========================================================================*/
__HIGH_CODE
static void ble_apply_regs(void)
{
    if (!s_regs_saved)
    {
        s_saved_misc = R32_MISC_CTRL;
        s_saved_det3 = REG_PKT_DET_CFG3;
        s_saved_det4 = REG_PKT_DET_CFG4;
        s_regs_saved = 1;
    }
    sys_safe_access_enable();
    R32_MISC_CTRL = (R32_MISC_CTRL & ~(0x3Fu << 24)) | (0x0Eu << 24);
    sys_safe_access_disable();
    REG_PKT_DET_CFG4 = 0x3Fu;
    REG_PKT_DET_CFG3 = (REG_PKT_DET_CFG3 & ~0x3FFu) | (PKT_DET_TIMACQ_1M_ADDR4 & 0x3FFu);
}

__HIGH_CODE
static void ble_restore_regs(void)
{
    if (!s_regs_saved) return;
    sys_safe_access_enable();
    R32_MISC_CTRL = s_saved_misc;
    sys_safe_access_disable();
    REG_PKT_DET_CFG3 = s_saved_det3;
    REG_PKT_DET_CFG4 = s_saved_det4;
}

__HIGH_CODE
static uint8_t ble_send_on_channel(uint8_t rf_freq, uint8_t ble_ch)
{
    uint32_t guard;

    s_tx.frequency    = rf_freq;
    s_tx.whiteChannel = ble_ch;
    s_tx.txLen        = s_tx_len;

    s_tx_done = 0;
    if (RFIP_StartTx(&s_tx) != 0) return 1;

    guard = RF_TX_TIMEOUT_LOOPS;
    while ((s_tx_done == 0) && (--guard != 0))
    {
        __asm volatile("nop");
        __asm volatile("nop");
    }
    return (guard == 0) ? 1 : 0;
}

__HIGH_CODE void BLE_AdvBurst(void)
{
    g_ble_adv_active = 1;
    ble_apply_regs();

    /* RFIP frequency is the BLE channel number, not the MHz offset. */
    ble_send_on_channel(BLE_ADV_CH37_FREQ, BLE_ADV_CH37_IDX);
    mDelayuS(100);
    ble_send_on_channel(BLE_ADV_CH38_FREQ, BLE_ADV_CH38_IDX);
    mDelayuS(100);
    ble_send_on_channel(BLE_ADV_CH39_FREQ, BLE_ADV_CH39_IDX);

    ble_restore_regs();
    g_ble_adv_active = 0;
}

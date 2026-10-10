/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH / Modified for Wireless Current Sensor Tool
 * Version            : V2.0
 * Date               : 2026/09/23
 * Description        : Probe Main Entry
 *******************************************************************************/

#include <rf.h>
#include <rf_uart_tx.h>
#include <uart.h>
#include "ina226.h"
#include "log.h"

extern dev_config_t g_dev_config;
void Config_Load(void);

__attribute__((used))
void process_main( void )
{
    while(1)
    {
        RF_StatusQuery();
        MAX811_Poll();
#if PROBE_WITHOUT_UART
        Probe_LED_Poll();
#endif
    }
}

int main(void)
{
    HSECFG_Capacitance(HSECap_8p);
    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);

    /* Enable SysTick free-running counter for accurate timestamps and heartbeat */
    SysTick->CMP = 0xFFFFFFFF;
    SysTick->CTLR = (1 << 2) | (1 << 0); // STCLK | STE

    /* The temporary Probe build dedicates PA0/PA1 to LED indicators. */
#if PROBE_WITHOUT_UART
    MAX811_Init();
#else
    UART_Init();
#endif

    Config_Load();
#if !PROBE_WITHOUT_UART
    UART_SwapPins( g_dev_config.uart_swapped );
#endif

    INA226_Init( g_dev_config.shunt_uohm, g_dev_config.avg_samples );
#if PROBE_WITHOUT_UART
    Probe_LED_Init();
#endif
    RFRole_Init();
    RF_UartTxInit();

    process_main();
}

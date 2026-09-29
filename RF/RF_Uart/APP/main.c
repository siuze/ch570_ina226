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

extern dev_config_t g_dev_config;
void Config_Load(void);

__attribute__((used))
__HIGH_CODE
void process_main( void )
{
    while(1)
    {
        RF_StatusQuery();
    }
}

int main(void)
{
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(CLK_SOURCE_HSE_PLL_24MHz);

    Config_Load();

    UART_Init();
    if( g_dev_config.uart_swapped )
    {
        UART_SwapPins( 1 );
    }

    INA226_Init( g_dev_config.shunt_uohm, g_dev_config.avg_samples );

    PRINT("Probe started.\n");
    PRINT("%s\n", VER_RF_LIB);
    RFRole_Init();
    RF_UartTxInit();
    process_main();
}

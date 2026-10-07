/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH / Modified for Wireless Current Sensor Tool
 * Version            : V2.0
 * Date               : 2026/09/23
 * Description        : Dongle Main Entry
 *******************************************************************************/

#include "rf.h"
#include "rf_uart_rx.h"
#include "usb_uart.h"

__attribute__((used))
__HIGH_CODE
void process_main( void )
{
    while(1)
    {
        USB_StatusQuery();
    }
}

int main(void)
{
    HSECFG_Capacitance( HSECap_8p );
    SetSysClock( CLK_SOURCE_HSE_PLL_100MHz );

    /* Enable SysTick free-running counter */
    SysTick->CMP = 0xFFFFFFFF;
    SysTick->CTLR = (1 << 2) | (1 << 0);

    /* PA0/PA1 are USB pins; the debug UART is not initialized here. */
    USB_Init();
    RFRole_Init();
    RF_UartRxInit();
    /* RFRole_Init may gate/reset unused peripheral clocks.  Configure the
     * LED PWM after RF startup so PWMX remains enabled. */
    LED1_Init(); // PA3 / PWM3: link / comm-status indicator
    LED2_Init(); // PA2 / PWM2: current indicator

    process_main();
}

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
    HSECFG_Capacitance( HSECap_18p );
    SetSysClock( CLK_SOURCE_HSE_PLL_100MHz );

    // LED 1 (PA3) Link / comm-status indicator (schematic LED202)
    GPIOA_SetBits( LED_PIN );
    GPIOA_ModeCfg( LED_PIN, GPIO_ModeOut_PP_5mA );

    // LED 2 (PA2) PWM2 Current Indicator (schematic LED201)
    LED2_PWM_Init();

    PRINT("Dongle starting...\n");
    PRINT("%s\n", VER_RF_LIB);

    USB_Init();
    RFRole_Init();
    RF_UartRxInit();

    process_main();
}

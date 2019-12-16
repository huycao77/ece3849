/*
 * comparator.c
 *
 *  Created on: Dec 6, 2019
 *      Author: quoch
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/cfg/global.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/gates/GateHwi.h>
#include "driverlib/comp.h"
#include "driverlib/pin_map.h"
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/timer.h"
#include "driverlib/interrupt.h"
#include "driverlib/adc.h"
#include "sysctl_pll.h"
#include "inc/tm4c1294ncpdt.h"
#include "sampling.h"
#include "buttons.h"
#include "comparator.h"
#include "Crystalfontz128x128_ST7735.h"
#include "driverlib/udma.h"

//Global
volatile uint32_t period = 0;
volatile uint32_t last_count = 0;
volatile uint32_t acc_interval = 0;
volatile int total_period = 0;

//Initialize Analog Comparator
void Comparator_Init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_COMP0);
    ComparatorRefSet(COMP_BASE, COMP_REF_1_65V);//set internal ref voltage to 1.65V
    ComparatorConfigure(COMP_BASE, 1, (COMP_INT_RISE | COMP_TRIG_NONE | COMP_ASRCP_REF | COMP_OUTPUT_NORMAL)); //config the comparator to use internal reference voltage

    // configure GPIO for comparator input C1- at BoosterPack Connector #1 pin 3
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
    GPIOPinTypeComparator(GPIO_PORTC_BASE, GPIO_PIN_4);

    // configure GPIO for comparator output C1o at BoosterPack Connector #1 pin 15
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    GPIOPinTypeComparatorOutput(GPIO_PORTD_BASE, GPIO_PIN_1);
    GPIOPinConfigure(GPIO_PD1_C1O);

}

void Timer_Capture_Init(void)
{
    // configure GPIO PD0 as timer input T0CCP0 at BoosterPack Connector #1 pin 14
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    GPIOPinTypeTimer(GPIO_PORTD_BASE, GPIO_PIN_0);
    GPIOPinConfigure(GPIO_PD0_T0CCP0);

    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    TimerDisable(TIMER0_BASE, TIMER_BOTH);
    TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_CAP_TIME_UP);
    TimerControlEvent(TIMER0_BASE, TIMER_A, TIMER_EVENT_POS_EDGE);
    TimerLoadSet(TIMER0_BASE, TIMER_A, 0xffff);   // use maximum load value
    TimerPrescaleSet(TIMER0_BASE, TIMER_A, 0xff); // use maximum prescale value
    TimerIntEnable(TIMER0_BASE, TIMER_CAPA_EVENT);
    TimerEnable(TIMER0_BASE, TIMER_A);
}

void Timer0A_ISR (void)
{
    TimerIntClear(TIMER0_BASE, TIMER_CAPA_EVENT);

    //get timer count and calculate the period
    uint32_t count = TimerValueGet(TIMER0_BASE, TIMER_A);
    period = (count - last_count) & 0xffffff;
    last_count = count;
    acc_interval += period;
    total_period++;
}


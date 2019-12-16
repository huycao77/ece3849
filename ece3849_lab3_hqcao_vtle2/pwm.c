/*
 * pwm.c
 *
 *  Created on: Dec 9, 2019
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
#include "inc/hw_memmap.h"
#include "driverlib/pin_map.h"
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
#include "driverlib/pwm.h"
#include "inc/tm4c1294ncpdt.h"
#include <math.h>

#define PWM_PERIOD 258  // PWM period = 2^8 + 2 system clock cycles
#define PWM_WAVEFORM_INDEX_BITS 10
#define PWM_WAVEFORM_TABLE_SIZE (1 << PWM_WAVEFORM_INDEX_BITS)
#define PI 3.14159265358979f

uint32_t gPhase = 0;              // phase accumulator
uint32_t gPhaseIncrement = 156981055;     // phase increment for 16 kHz
uint8_t gPWMWaveformTable[PWM_WAVEFORM_TABLE_SIZE] = {0};


void PWM_Init(void)
{
    // use M0PWM1, at GPIO PF1, which is BoosterPack Connector #1 pin 40
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    GPIOPinTypePWM(GPIO_PORTF_BASE, GPIO_PIN_1); // PF1 = M0PWM1
    GPIOPinConfigure(GPIO_PF1_M0PWM1);
    GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_STRENGTH_8MA, GPIO_PIN_TYPE_STD);

    // configure the PWM0 peripheral
    SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM0);
    PWMClockSet(PWM0_BASE, PWM_SYSCLK_DIV_1);             // use system clock
    PWMGenConfigure(PWM0_BASE, PWM_GEN_0, PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC);
    PWMGenPeriodSet(PWM0_BASE, PWM_GEN_0, PWM_PERIOD);
    PWMPulseWidthSet(PWM0_BASE, PWM_OUT_1, PWM_PERIOD/2); // initial 50% duty cycle
    PWMOutputInvert(PWM0_BASE, PWM_OUT_1_BIT, true);      // invert PWM output
    PWMOutputState(PWM0_BASE, PWM_OUT_1_BIT, true);       // enable PWM output
    PWMGenEnable(PWM0_BASE, PWM_GEN_0);                   // enable PWM generator

    // enable PWM interrupt in the PWM peripheral
    PWMGenIntTrigEnable(PWM0_BASE, PWM_GEN_0, PWM_INT_CNT_ZERO);
    PWMIntEnable(PWM0_BASE, PWM_INT_GEN_0);

    //create waveform table
    int j;

    for (j = 0; j < PWM_WAVEFORM_TABLE_SIZE; j++)
    {
        gPWMWaveformTable[j] = 128 + sinf(2.f*PI*j/PWM_WAVEFORM_TABLE_SIZE)*127;
    }

}

void PWM_ISR(void)
{
    PWMGenIntClear(PWM0_BASE, PWM_GEN_0, PWM_INT_CNT_ZERO); // clear PWM interrupt flag

    gPhase += gPhaseIncrement;

    // write directly to the Compare B register that determines the duty cycle
    PWM0_0_CMPB_R = 1 + gPWMWaveformTable[gPhase >> (32 - PWM_WAVEFORM_INDEX_BITS)];
}



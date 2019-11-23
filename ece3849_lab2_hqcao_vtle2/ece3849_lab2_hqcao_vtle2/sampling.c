/*
 * sampling.c
 *
 *  Created on: Oct 30, 2019
 *      Author: hqcao
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
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
#include "Crystalfontz128x128_ST7735.h"

#define ADC_BUFFER_SIZE 2048                             // size must be a power of 2
#define ADC_BUFFER_WRAP(i) ((i) & (ADC_BUFFER_SIZE - 1)) // index wrapping macro
volatile int32_t gADCBufferIndex = ADC_BUFFER_SIZE - 1;  // latest sample index
volatile uint16_t gADCBuffer[ADC_BUFFER_SIZE];           // circular buffer
volatile uint32_t gADCErrors;                       // number of missed ADC deadlines

// Step 2: ADC sampling
void ADCInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_0);          // GPIO setup for analog input AIN3

    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0); // initialize ADC peripherals
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC1);

    // ADC clock
    uint32_t pll_frequency = SysCtlFrequencyGet(CRYSTAL_FREQUENCY);
    uint32_t pll_divisor = (pll_frequency - 1) / (16 * ADC_SAMPLING_RATE) + 1; //round up
    ADCClockConfigSet(ADC0_BASE, ADC_CLOCK_SRC_PLL | ADC_CLOCK_RATE_FULL, pll_divisor);
    ADCClockConfigSet(ADC1_BASE, ADC_CLOCK_SRC_PLL | ADC_CLOCK_RATE_FULL, pll_divisor);
    ADCSequenceDisable(ADC1_BASE, 0);      // choose ADC1 sequence 0; disable before configuring
    ADCSequenceConfigure(ADC1_BASE, 0, ADC_TRIGGER_ALWAYS, 0);    // specify the "Always" trigger
    ADCSequenceStepConfigure(ADC1_BASE, 0, 0, ADC_CTL_CH3|ADC_CTL_IE|ADC_CTL_END);// in the 0th step, sample channel 3 (AIN3)
    // enable interrupt, and make it the end of sequence
    ADCSequenceEnable(ADC1_BASE, 0);       // enable the sequence.  it is now sampling
    ADCIntEnable(ADC1_BASE, 0);            // enable sequence 0 interrupt in the ADC1 peripheral
}


// ADC ISR
void ADC_ISR(void)
{
    ADC1_ISC_R = ADC_ISC_IN0; // clear ADC1 sequence0 interrupt flag in the ADCISC register
    if (ADC1_OSTAT_R & ADC_OSTAT_OV0) { // check for ADC FIFO overflow
        gADCErrors++;                   // count errors
        ADC1_OSTAT_R = ADC_OSTAT_OV0;   // clear overflow condition
    }
    gADCBuffer[
               gADCBufferIndex = ADC_BUFFER_WRAP(gADCBufferIndex + 1)
               ] = ADC1_SSFIFO0_R ;               // read sample from the ADC1 sequence 0 FIFO
}

//Trigger search function
//trigger_mode = 1 for rising edge, trigger_mode = 0 for falling edge
uint32_t ADC_Trigger_Search (uint8_t trigger_mode, uint32_t trigger_level)
{
    uint32_t index = ADC_BUFFER_WRAP(gADCBufferIndex - (LCD_HORIZONTAL_MAX/2));
    uint32_t trigger_index = index;
    uint32_t adc_values = gADCBuffer[trigger_index];
    uint32_t adc_values_prev;
    uint32_t trigger_search_max = ADC_BUFFER_SIZE / 2;//half of the ADC buffer size
    uint32_t sample_num = 0;

    //Tranversing half of the ADC buffer
    while (++sample_num < trigger_search_max)
    {
        adc_values_prev = gADCBuffer[trigger_index];
        trigger_index = trigger_index - 1;
        adc_values = gADCBuffer[trigger_index];

        if ((trigger_mode) && (adc_values > trigger_level) && (adc_values_prev < trigger_level)) return trigger_index; //rising mode
        else if ((!trigger_mode) && (adc_values < trigger_level) && (adc_values_prev > trigger_level)) return trigger_index;//falling mode
    }

    return index; //give up if no trigger is founded after half of the ADC buffer
}



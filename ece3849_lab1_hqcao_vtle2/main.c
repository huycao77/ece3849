/**
 * main.c
 *
 * ECE 3849 Lab 0 Starter Project
 * Gene Bogdanov    10/18/2017
 *
 * This version is using the new hardware for B2017: the EK-TM4C1294XL LaunchPad with BOOSTXL-EDUMKII BoosterPack.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include "driverlib/fpu.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "Crystalfontz128x128_ST7735.h"
#include "inc/tm4c1294ncpdt.h"
#include "driverlib/timer.h"
#include "inc/hw_memmap.h"
#include <stdio.h>
#include <math.h>
#include "buttons.h"
#include "sampling.h"

//Macros definition
#define FIFO_SIZE 11

//Global declaration
uint32_t gSystemClock; // [Hz] system clock frequency
volatile uint32_t gTime = 8345; // time in hundredths of a second
volatile char bit[10];
typedef char DataType;      // FIFO data type
volatile DataType fifo[FIFO_SIZE];  // FIFO storage array
volatile int fifo_head = 0; // index of the first item in the FIFO
volatile int fifo_tail = 0; // index one step past the last item
float cpu_unloaded;
float cpu_loaded;
const char * const gVoltageScaleStr[] =
{"100 mV", "200 mV", "500 mV", "1V"};
float Vscale[] = {0.1,0.2,0.5,1};

uint32_t cpu_load_count(void)
{
    uint32_t i = 0;
    TimerIntClear(TIMER3_BASE, TIMER_TIMA_TIMEOUT);
    TimerEnable(TIMER3_BASE, TIMER_A); // start one-shot timer
    while (!(TimerIntStatus(TIMER3_BASE, false) & TIMER_TIMA_TIMEOUT))
        i++;
    return i;
}

int fifo_put(DataType data)
{
    int new_tail = fifo_tail + 1;
    if (new_tail >= FIFO_SIZE) new_tail = 0; // wrap around
    if (fifo_head != new_tail) {    // if the FIFO is not full
        fifo[fifo_tail] = data;     // store data into the FIFO
        fifo_tail = new_tail;       // advance FIFO tail index
        return 1;                   // success
    }
    return 0;   // full
}

// get data from the FIFO
// returns 1 on success, 0 if FIFO was empty
int fifo_get(DataType *data)
{
    if (fifo_head != fifo_tail) {   // if the FIFO is not empty
        *data = fifo[fifo_head];    // read data from the FIFO

        if (fifo_head == FIFO_SIZE - 1)
            fifo_head = 0;
        else
            fifo_head++;

        return 1;                   // success
    }
    return 0;   // empty
}

int main(void)
{
    IntMasterDisable();

    // Enable the Floating Point Unit, and permit ISRs to use it
    FPUEnable();
    FPULazyStackingEnable();

    // Initialize the system clock to 120 MHz
    gSystemClock = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480, 120000000);

    Crystalfontz128x128_Init(); // Initialize the LCD display driver
    Crystalfontz128x128_SetOrientation(LCD_ORIENTATION_UP); // set screen orientation

    tContext sContext;
    GrContextInit(&sContext, &g_sCrystalfontz128x128); // Initialize the grlib graphics context
    GrContextFontSet(&sContext, &g_sFontFixed6x8); // select font

    //Initialize Timer 3
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER3);
    TimerDisable(TIMER3_BASE, TIMER_BOTH);
    TimerConfigure(TIMER3_BASE, TIMER_CFG_ONE_SHOT);
    TimerLoadSet(TIMER3_BASE, TIMER_A, gSystemClock/100 - 1); // 1 sec interval

    //Initialize button and interrupt
    ButtonInit();
    ADCInit();
    cpu_unloaded = cpu_load_count();
    IntMasterEnable();

    //Local variable
    uint16_t button = 0;
    int16_t sample[128];
    int vscale_index = 1;
    int trigger_slope;
    int y1 = 0,y2 = 0,x1 = 0;
    int i = 0;
    int j = 0; int x = 0;
    int k = 0;
    float fScale = 0.0;
    char but_char;
    float cpu_load;
    char time_scale[] = "20 us";   // string buffer
    char cpu[50];
    // full-screen rectangle
    tRectangle rectFullScreen = {0, 0, GrContextDpyWidthGet(&sContext)-1, GrContextDpyHeightGet(&sContext)-1};

    while (true) {
        //Get FIFO button character
        while (fifo_get(&but_char))
        {
            switch (but_char)
            {
            case 'U':
                if (vscale_index < 3) vscale_index++;
                break;
            case 'D':
                if (vscale_index > 0) vscale_index--;
                break;
            case 'R':
                trigger_slope = 1;
                break;
            case 'F':
                trigger_slope = 0;
                break;
            }
        }
        GrContextForegroundSet(&sContext, ClrBlack);
        GrRectFill(&sContext, &rectFullScreen); // fill screen with black

        //draw the grid
        GrContextForegroundSet(&sContext, ClrBlue);
        for (k = 0; k <= 7; k++)
        {
            GrLineDrawV(&sContext,(PIXELS_PER_DIV*k + 4),0,LCD_VERTICAL_MAX);
        }
        for (j = 0; j <= 7; j++)
        {
            GrLineDrawH(&sContext,0,LCD_HORIZONTAL_MAX,(PIXELS_PER_DIV*j + 4));
        }

        //Draw waveform
        GrContextForegroundSet(&sContext, ClrYellow); // yellow text
        int trigger = ADC_Trigger_Search(trigger_slope,ADC_OFFSET);
        for (i = 0; i < 128; i++)
        {
            sample[i] = gADCBuffer[ADC_BUFFER_WRAP(trigger-(LCD_HORIZONTAL_MAX/2)+i)]; //local copy of ADC buffer
        }
        fScale = (VIN_RANGE * PIXELS_PER_DIV)/((1 << ADC_BITS)*Vscale[vscale_index]);
        y1 = LCD_VERTICAL_MAX/2 - (int)roundf(fScale*((int)sample[0]-ADC_OFFSET));
        for (x = 1; x < LCD_HORIZONTAL_MAX;x++)
        {
            y2 = LCD_VERTICAL_MAX/2 - (int)roundf(fScale*((int)sample[x]-ADC_OFFSET));
            GrLineDraw(&sContext,x-1,y1,x,y2);
            y1 = y2;
        }

        //Draw time scale, voltage scale
        GrContextForegroundSet(&sContext, ClrWhite);
        GrStringDraw(&sContext, time_scale, -1,0,0,false);
        GrStringDraw(&sContext, gVoltageScaleStr[vscale_index],-1,50,0,false);

        //Draw trigger slope
        if(trigger_slope)
        {
            GrLineDraw(&sContext,105,10,110,10);
            GrLineDraw(&sContext,110,10,110,0);
            GrLineDraw(&sContext,110,0,115,0);
            GrLineDraw(&sContext,108,6,110,4);
            GrLineDraw(&sContext,110,4,112,6);
        }
        else
        {
            GrLineDraw(&sContext,105,0,110,0);
            GrLineDraw(&sContext,110,10,110,0);
            GrLineDraw(&sContext,110,10,115,10);
            GrLineDraw(&sContext,108,4,110,6);
            GrLineDraw(&sContext,110,6,112,4);
        }

        //Draw CPU Load
        cpu_loaded = cpu_load_count();
        cpu_load = (1.0f - (float)cpu_loaded/cpu_unloaded)*100;
        snprintf(cpu, sizeof(cpu), "CPU load = %.1f %%", cpu_load);
        GrStringDraw(&sContext, cpu, -1,0,120,false);

        GrFlush(&sContext); // flush the frame buffer to the LCD
    }
}



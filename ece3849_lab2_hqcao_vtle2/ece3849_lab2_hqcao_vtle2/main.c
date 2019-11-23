/*
 * ECE 3849 Lab2 starter project
 *
 * Gene Bogdanov    9/13/2017
 */
/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/cfg/global.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>

#include <stdint.h>
#include <stdbool.h>
#include "driverlib/interrupt.h"
#include "driverlib/fpu.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "driverlib/gpio.h"
#include "Crystalfontz128x128_ST7735.h"
#include "inc/tm4c1294ncpdt.h"
#include "driverlib/timer.h"
#include "inc/hw_memmap.h"
#include <stdio.h>
#include <math.h>
#include "buttons.h"
#include "sampling.h"
#include "kiss_fft.h"
#include "_kiss_fft_guts.h"

/* Define */
#define PI 3.14159265358979f
#define NFFT 1024 // FFT length
#define KISS_FFT_CFG_SIZE (sizeof(struct kiss_fft_state)+sizeof(kiss_fft_cpx)*(NFFT-1))

/* Global declaration */
uint32_t gSystemClock = 120000000; // [Hz] system clock frequency
volatile int vscale_index = 1;
volatile int trigger_slope = 1;
volatile int fft_mode = 0;
volatile float fScale = 0.0;
volatile int x,y1,y2;
float Vscale[] = {0.1,0.2,0.5,1};
static volatile int ycoor[LCD_HORIZONTAL_MAX+1];
static volatile int ycoor_fft[NFFT];

static volatile int16_t sample[128];
static volatile int16_t fft_sample[NFFT];

static char kiss_fft_cfg_buffer[KISS_FFT_CFG_SIZE]; // Kiss FFT config memory
size_t buffer_size = KISS_FFT_CFG_SIZE;
kiss_fft_cfg cfg; // Kiss FFT config
static kiss_fft_cpx in[NFFT], out[NFFT]; // complex waveform and spectrum buffers
int i;

/*
 *  ======== main ========
 */
int main(void)
{
    IntMasterDisable();

    // hardware initialization goes here
    ButtonInit();
    ADCInit();

    Crystalfontz128x128_Init(); // Initialize the LCD display driver
    Crystalfontz128x128_SetOrientation(LCD_ORIENTATION_UP); // set screen orientation

    /* Start BIOS */
    BIOS_start();

    return (0);
}

void buttonclock_func(UArg arg1)
{
    Semaphore_post(button_sem);
}

void buttontask_func(UArg arg1, UArg arg2)
{
    IntMasterEnable();
    char but;
    while (true) {
        Semaphore_pend(button_sem, BIOS_WAIT_FOREVER);

        // read hardware button state
        uint32_t gpio_buttons =
                (~GPIOPinRead(GPIO_PORTJ_BASE, 0xff) & (GPIO_PIN_1 | GPIO_PIN_0))
                | ((~GPIOPinRead(GPIO_PORTH_BASE, 0xff) & (GPIO_PIN_1))<<1)
                | (~GPIOPinRead(GPIO_PORTD_BASE, 0xff) & (GPIO_PIN_4))
                | ((~GPIOPinRead(GPIO_PORTK_BASE, 0xff) & (GPIO_PIN_6))>>3); // EK-TM4C1294XL buttons in positions 0 and 1

        uint32_t old_buttons = gButtons;    // save previous button state
        ButtonDebounce(gpio_buttons);       // Run the button debouncer. The result is in gButtons.
        ButtonReadJoystick();               // Convert joystick state to button presses. The result is in gButtons.
        uint32_t presses = ~old_buttons & gButtons;   // detect button presses (transitions from not pressed to pressed)
        presses |= ButtonAutoRepeat();      // autorepeat presses if a button is held long enough

        but = '\0';
        if (presses & 4) but = 'U'; //Button S1
        if (presses & 8) but = 'D'; //button S2
        if (presses & 64) but = 'S'; //joystick LEFT
        if (presses & 128) but = 'R';//joystick UP
        if (presses & 256) but = 'F';//joystick DOWN

        if (but != '\0')
            Mailbox_post(button_mailbox, &but, BIOS_WAIT_FOREVER);
    }
}

void UI_func(UArg arg1, UArg arg2)
{
    IntMasterEnable();
    char but_char;

    while (true) {
        Mailbox_pend(button_mailbox, &but_char, BIOS_WAIT_FOREVER);
        //Get mailbox button ID
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
        case 'S':
            fft_mode = !fft_mode;
            break;
        }

        Semaphore_post(display_sem);
    }
}

void waveform_func(UArg arg1, UArg arg2)
{
    IntMasterEnable();
    int i;
    int j = 0;
    while (true) {
        Semaphore_pend(waveform_sem, BIOS_WAIT_FOREVER);
        //fft_mode = 0 for normal oscilloscope function
        if (!fft_mode){
            int trigger = ADC_Trigger_Search(trigger_slope,ADC_OFFSET);
            for (i = 0; i < 128; i++)
            {
                sample[i] = gADCBuffer[ADC_BUFFER_WRAP(trigger-(LCD_HORIZONTAL_MAX/2)+i)]; //local copy of ADC buffer
            }
        }
        //fft_mode = 1 for Spectrum Analyzer mode
        else
        {
            //capture the newest 1024 samples from the ADC without searching for trigger
            for(j=0;j<NFFT;j++)
            {
                fft_sample[j] = gADCBuffer[ADC_BUFFER_WRAP(gADCBufferIndex - j)];
            }
        }
        Semaphore_post(processing_sem);
    }
}

void display_func(UArg arg1, UArg arg2)
{
    int j,k;
    const char * const gVoltageScaleStr[] =
    {"100 mV", "200 mV", "500 mV", "1V"};
    char time_scale[] = "20 us";
    char f_scale[] = "20 kHz";
    char dB_v[] = "20 dBV";
    tContext sContext;

    GrContextInit(&sContext, &g_sCrystalfontz128x128); // Initialize the grlib graphics context
    GrContextFontSet(&sContext, &g_sFontFixed6x8); // select font
    tRectangle rectFullScreen = {0, 0, GrContextDpyWidthGet(&sContext)-1, GrContextDpyHeightGet(&sContext)-1};

    while (true) {
        Semaphore_pend(display_sem, BIOS_WAIT_FOREVER);
        //fft_mode = 0 for normal oscilloscope function
        if (!fft_mode){
            int local_vscale = vscale_index;
            int local_trigger = trigger_slope;
            GrContextForegroundSet(&sContext, ClrBlack);
            GrRectFill(&sContext, &rectFullScreen); // fill screen with black

            //draw the grid
            GrContextForegroundSet(&sContext, ClrBlue);
            for (k = 0; k <= 7; k++)
            {
                GrLineDrawV(&sContext,(PIXELS_PER_DIV*k+4),0,LCD_VERTICAL_MAX);
            }
            for (j = 0; j <= 7; j++)
            {
                GrLineDrawH(&sContext,0,LCD_HORIZONTAL_MAX,(PIXELS_PER_DIV*j+4));
            }

            GrContextForegroundSet(&sContext, ClrYellow); // yellow text
            for (x = 1; x < LCD_HORIZONTAL_MAX;x++)
            {
                int y1 = ycoor[x-1];
                int y2 = ycoor[x];
                GrLineDraw(&sContext,x-1,y1,x,y2);
            }
            //Draw time scale, voltage scale
            GrContextForegroundSet(&sContext, ClrWhite);
            GrStringDraw(&sContext, time_scale, -1,0,0,false);
            GrStringDraw(&sContext, gVoltageScaleStr[local_vscale],-1,50,0,false);

            //Draw trigger slope
            if(local_trigger)
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
            GrFlush(&sContext);
        }

        //fft_mode = 1 for Spectrum Analyzer Mode
        else
        {
            GrContextForegroundSet(&sContext, ClrBlack);
            GrRectFill(&sContext, &rectFullScreen); // fill screen with black

            //draw the grid
            GrContextForegroundSet(&sContext, ClrBlue);
            for (k = 0; k <= 7; k++)
            {
                GrLineDrawV(&sContext,(PIXELS_PER_DIV*k),0,LCD_VERTICAL_MAX);
            }
            for (j = 0; j <= 7; j++)
            {
                GrLineDrawH(&sContext,0,LCD_HORIZONTAL_MAX,(PIXELS_PER_DIV*j));
            }

            GrContextForegroundSet(&sContext, ClrYellow); // yellow text
            for (x = 1; x < LCD_HORIZONTAL_MAX;x++)
            {
                int y1 = ycoor_fft[x-1];
                int y2 = ycoor_fft[x];
                GrLineDraw(&sContext,x-1,y1,x,y2);
            }
            //Draw time scale, voltage scale
            GrContextForegroundSet(&sContext, ClrWhite);
            GrStringDraw(&sContext, f_scale, -1,10,0,false);
            GrStringDraw(&sContext, dB_v,-1,60,0,false);
            GrFlush(&sContext);
        }
    }
}

void processing_func(UArg arg1, UArg arg2)
{
    IntMasterEnable();
    int i = 0;
    float mag = 0;
    cfg = kiss_fft_alloc(NFFT, 0, kiss_fft_cfg_buffer, &buffer_size); // init Kiss FFT
    static float w[NFFT]; // window function

    for (i = 0; i < NFFT; i++) {
        // Blackman window
        w[i] = 0.42f - 0.5f * cosf(2*PI*i/(NFFT-1)) + 0.08f * cosf(4*PI*i/(NFFT-1));
    }

    while (true) {
        Semaphore_pend(processing_sem, BIOS_WAIT_FOREVER);
        //fft_mode = 0 for normal oscilloscope function
        if (!fft_mode)
        {
            fScale = (VIN_RANGE * PIXELS_PER_DIV)/((1 << ADC_BITS)*Vscale[vscale_index]);
            //stored the processed waveform into another global buffer
            for (i = 0; i <= LCD_HORIZONTAL_MAX; i++){
                ycoor[i] = LCD_VERTICAL_MAX/2 - (int)roundf(fScale*((int)sample[i]-ADC_OFFSET));
            }
        }
        //fft_mode = 1 for Spectrum Analyzer mode
        else
        {
            for (i = 0; i < NFFT; i++) { // generate an input waveform
                in[i].r = fft_sample[i]*w[i]; // real part of waveform
                in[i].i = 0; // imaginary part of waveform
            }
            kiss_fft(cfg, in, out); // compute FFT
            for (i = 0; i < NFFT; i++)
            {
                mag = sqrtf((out[i].r*out[i].r)+(out[i].i*out[i].i));//compute magnitude of the resulting FFT
                ycoor_fft[i] = (-20*log10f(mag))+128;//compute dB, stored to processed waveform buffer
            }
        }
        Semaphore_post(waveform_sem);
        Semaphore_post(display_sem);
    }
}


/*
 * sampling.h
 *
 *  Created on: Oct 30, 2019
 *      Author: hqcao
 */

#ifndef SAMPLING_H_
#define SAMPLING_H_


//Function declarations
void ADCInit(void);
void ADC_ISR(void);
uint32_t ADC_Trigger_Search (uint8_t trigger_mode, uint32_t trigger_level);

//Macros
#define VIN_RANGE                       3.3
#define PIXELS_PER_DIV                  20
#define ADC_BITS                        12
#define ADC_OFFSET                      2040 //to be changed later
#define ADC_BUFFER_SIZE                 2048
#define ADC_BUFFER_WRAP(i) ((i) & (ADC_BUFFER_SIZE - 1))

//Variables
extern volatile uint16_t gADCBuffer[ADC_BUFFER_SIZE];
#endif /* SAMPLING_H_ */

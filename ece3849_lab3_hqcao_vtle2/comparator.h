/*
 * comparator.h
 *
 *  Created on: Dec 6, 2019
 *      Author: quoch
 */

#ifndef COMPARATOR_H_
#define COMPARATOR_H_

//Function declaration
void Comparator_Init(void);
void Timer_Capture_Init(void);

//Global
extern volatile uint32_t period;
extern volatile uint32_t last_count;
extern volatile uint32_t acc_interval;
extern volatile int total_period;
#endif /* COMPARATOR_H_ */

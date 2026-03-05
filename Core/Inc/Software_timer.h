/*
 * Software_timer.h
 *
 *  Created on: Feb 20, 2026
 *      Author: saerj
 */

#ifndef INC_SOFTWARE_TIMER_H_
#define INC_SOFTWARE_TIMER_H_
#include <stdbool.h>
typedef struct {
    uint32_t startTime;
    uint32_t interval;   // เวลาที่ต้องการ
    bool isRunning;
} SoftwareTimer;


void Timer_Init(SoftwareTimer*, uint32_t);
void Timer_Start(SoftwareTimer*);
bool Timer_Expired(SoftwareTimer*);


#endif /* INC_SOFTWARE_TIMER_H_ */

/*
 * Software_timer.c
 *
 *  Created on: Feb 20, 2026
 *      Author: saerj
 */
#include "main.h"
#include "app_header.h"
#include "stdbool.h"
#include "string.h"
#include "stdio.h"
#include "Software_timer.h"


void Timer_Init(SoftwareTimer* timer, uint32_t interval) {
    timer->interval = interval;
    timer->isRunning = false;
}

// start a software timer
void Timer_Start(SoftwareTimer* timer) {
    timer->startTime = uwTick; // Capture current tick
    timer->isRunning = true;
}

// check if a software timer has expired
bool Timer_Expired(SoftwareTimer* timer) {
    if (timer->isRunning && (uwTick - timer->startTime) >= timer->interval) {
        timer->isRunning = false; // Stop the timer after expiration
        // To make it a periodic timer, re-start it here:
        //timer->startTime += timer->interval;
        return true;
    }
    return false;
}

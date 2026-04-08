#ifndef POVOTOTASKS_H
#define POVOTOTASKS_H

#include <Arduino.h>

#define TASK_TIMEOUT_MIN 10

extern byte taskWindowType;
extern unsigned long taskWindowEndTime;

void startDumpTask();
void startGasTask();
void startLiquidTask();
void startDryHoppingTask();
void startDynamicHoppingTask();

void endDumpTask();
void endGasTask();
void endLiquidTask();
void endDryHoppingTask();
void endDynamicHoppingTask();

void checkTaskExpiration();

#endif // POVOTOTASKS_H

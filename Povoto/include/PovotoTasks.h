#ifndef POVOTOTASKS_H
#define POVOTOTASKS_H

#include <Arduino.h>

#define TASK_TIMEOUT_MIN 10

extern byte taskWindowType;
extern unsigned long taskWindowEndTime;
extern unsigned long lastTaskMillis;

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
// Ends the task window without its end-of-task processing (Conditioning entry).
void cancelActiveTask();
// Tasks are blocked in Conditioning (docs/conditioning.md).
bool tasksAllowed();

#endif // POVOTOTASKS_H

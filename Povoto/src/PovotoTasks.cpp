#include <Arduino.h>
#include "PovotoTasks.h"
#include "PovotoData.h"

byte taskWindowType = 0;
unsigned long taskWindowEndTime = 0;

static void startTask(byte type) {
  taskWindowType = type;
  taskWindowEndTime = millis() + (unsigned long)TASK_TIMEOUT_MIN * 60000UL;
}

static void endTask() {
  taskWindowEndTime = millis() + (unsigned long)CalibrationData.nucleationWindow * 60000UL;
}

void startDumpTask()           { startTask(1); }
void startGasTask()            { startTask(2); }
void startLiquidTask()         { startTask(3); }
void startDryHoppingTask()     { startTask(4); }
void startDynamicHoppingTask() { startTask(5); }

void endDumpTask() {
  // TODO: implement volume adjustment when dump task ends - lembrar de somar pATM
  taskWindowType = 0;
  taskWindowEndTime = 0;
}

void endGasTask() {
  endTask();
}

void endLiquidTask() {
  endTask();
}

void endDryHoppingTask() {
  endTask();
}

void endDynamicHoppingTask() {
  endTask();
}

void checkTaskExpiration() {
  if (taskWindowType != 0 && taskWindowEndTime != 0 && millis() > taskWindowEndTime) {
    taskWindowType = 0;
    taskWindowEndTime = 0;
  }
}

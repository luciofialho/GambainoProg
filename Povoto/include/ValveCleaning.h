#ifndef VALVE_CLEANING_H
#define VALVE_CLEANING_H

#include <Arduino.h>

// Valve cleaning cycle (docs/valve-cleaning.md): pressurized liquid on port A
// only, P and R to the drain. A run alternates the valve energized (A->P,
// counterflow over the seat) and de-energized (A->R, washes the plunger
// chamber) for the time chosen by the operator. Which liquids, in which order
// and when to change them is the operator's decision: the program controls no
// phase. The venting LED is on while the valve is closed during a run.

// Starts a run of `minutes` (0.5-15).
bool startValveCleaning(float minutes, char *reason, size_t reasonSize);
void abortValveCleaning();
bool valveCleaningActive();      // a run in progress
String valveCleaningStatusText();
void processValveCleaning();     // pressureControl(), main loop

#endif

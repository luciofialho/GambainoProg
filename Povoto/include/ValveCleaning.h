#ifndef VALVE_CLEANING_H
#define VALVE_CLEANING_H

#include <Arduino.h>

// Valve cleaning cycle (docs/valve-cleaning.md): pressurized liquid on port A
// only, P and R to the drain. Each phase alternates the valve energized
// (A->P, counterflow over the seat) and de-energized (A->R, washes the plunger
// chamber). Phases, each started by the operator after changing the liquid:
// neutral detergent, water, 70% alcohol, air.
constexpr uint8_t VALVE_CLEANING_PHASES = 4;
const char *valveCleaningPhaseName(uint8_t phase);

// Starts (or continues) the session with the next phase.
bool startValveCleaningPhase(float minutes, char *reason, size_t reasonSize);
void abortValveCleaning();
bool valveCleaningActive();      // session open (a phase running or waiting for the next)
bool valveCleaningRunning();     // a phase running
uint8_t valveCleaningNextPhase(); // 0..VALVE_CLEANING_PHASES (= all done)
String valveCleaningStatusText();
void processValveCleaning();     // pressureControl(), main loop

#endif

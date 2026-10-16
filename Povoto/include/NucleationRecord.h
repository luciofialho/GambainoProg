#ifndef NUCLEATION_RECORD_H
#define NUCLEATION_RECORD_H

#include <Arduino.h>

// Firmware side of the nucleation window estimate (include/NucleationEstimate.h,
// docs/dissolved-co2.md section 8). One calibration event per hopping task
// ("nucleation-estimate"); the firmware never changes nucleationWindow.

// A task started (any type) / ended (hopping tasks: 4 = dry hopping, 5 = dynamic hopping);
// a cancelled hopping task drops its reference. They only queue the event (the tasks end
// in the web task); the main loop handles it at the next CO2 sample.
void nucleationTaskStarted(byte taskType);
void nucleationTaskEnded(byte taskType);
void nucleationTaskCancelled();
// Once per CO2 sample (60 s, PressureControl.cpp): the gas-phase CO2 in mol, the gauge
// pressure and the externalStep flag of that sample. Runs the queued task events.
void nucleationEstimateSample(double gasMols, float gaugePressure, bool externalStep);
// One line for the Calibration page: progress, or empty.
String nucleationEstimateStatusText();

#endif

#ifndef POVOTO_DATALOG_H
#define POVOTO_DATALOG_H

#include <Arduino.h>

// One last Cold row and Brewfather point after entering Conditioning; after
// them nothing is sent while in Conditioning.
void requestConditioningFinalRecord();

// Snapshot captured after the post-vent pressure reading and its calculations.
// Keeping this separate from the live globals preserves values that are reset
// when the next vent cycle starts. The sheet keeps its name, Relief.
struct ReliefLogData {
  int povotoNumber;
  unsigned long reliefNumber;
  unsigned long valveOpenedMillis;
  unsigned long pressureReachedTargetMillis;
  unsigned long pressureAfterReliefMillis;
  float temperature;
  float targetPressure;
  float atmosphericPressure;
  const char *atmosphericPressureSource; // "sensor", "sidekick" or "altitude" (docs/atmospheric-pressure.md)
  float environmentTemperature;
  float gasTemperature;            // used for the flow and the headspace moles, C
  const char *gasTemperatureSource; // "headspace" or "liquid" (docs/thermometers.md)
  float effectiveVentingExponent;
  float pressureOnReliefMeasured;
  float currentOnReliefMeasured;
  float pressureReachedTarget;
  float pressureOnReliefExtrapolated;
  float pressureAfterRelief;       // read right after closing (gas still cold)
  float currentAfterRelief;
  float adjustedPressureAfterRelief; // settled, through the exponent
  // Vent (docs/vent.md).
  float ventEstimatedSeconds;      // target drop with the applied headspace
  float ventSafetySeconds;         // safety drop with the smallest plausible headspace
  float ventPlannedSeconds;
  const char *ventLimit;           // "estimate", "minimum" or "safety"
  float ventHeadspaceForTime;      // L
  float ventRealSeconds;           // measured at the digitalWrite of the valve
  float ventMeanPressure;          // log mean of the opening and closing pressures, gauge
  float ventCoolingFactor;
  float ventFlowConstant;          // CO2 (ratio, Z and level included)
  float flowLevelFactor;
  float ventFlowMoles;             // flow model, total moles (CO2 + vapor)
  float ventFermenterSideMoles;    // settled drop x reference headspace / RT
  const char *ventCounted;         // "flow", "fermenter-side" or "none"
  float ventDropPercent;           // settled drop / pressure at opening
  float ventTargetDropPercent;
  float ventVaporFraction;
  float ejectedMolsWithVapor;  // counted moles of the vent, vapor included (ejectedMols is the CO2 only)
  float instantaneousPressureDropFactor;
  float headSpaceVolume;
  float beerVolume;
  float ejectedMols;
  const char *ventHeadspaceStatus;
  double totalMolsEjected;
  float headSpaceCO2Mols;
  double dissolvedCO2Mols;
  double totalCO2Mols;
  float beerSG;
  unsigned long polytropicSourceReliefNumber;
  uint8_t polytropicSampleCount;
  float polytropicBackExtrapolatedPressure;
  float polytropicFitSlopeBarPerMinute;
  float polytropicEstimatedExponent;
  float polytropicFitRMSEBar;
  // [DAILY-HS] headSpaceVolume is the applied value; these explain where it came from.
  float headSpaceMeasured;   // this vent's apparent headspace when it fed the average (NAN = not used)
  float headSpaceApparent;   // flow moles / settled drop, always (NAN = not available)
  float headSpaceEMA;        // headspaceFiltered after the update
  float headSpaceDaily;      // 24-hour average (NAN = no hours)
  uint8_t dailyHours;
  const char *dailyState;    // "valid", "hold" or "ema"
  float reliefDropRatio;     // fermenter side / flow model = measured / expected drop (docs/alarms.md)
  const char *reliefCheck;   // "ok", "partial", "no-effect" or "" (not checked)
};

// One row of the Task log per finished or cancelled task, sent after the
// pressure samples that follow it (or earlier, when a new task starts).
constexpr uint8_t TASK_LOG_PRESSURE_SAMPLES = 4; // +1, +3, +5 and +10 min
struct TaskLogData {
  const char *task;       // "Dump", "Gas", ...
  const char *outcome;    // "finished", "timeout" or "cancelled"
  uint32_t startEpoch;    // local NTP epoch, 0 = no NTP
  uint32_t endEpoch;
  float durationSeconds;
  float temperature;      // beer, at the end
  float environmentTemperature;
  float atmosphericPressure;
  const char *co2Mode;    // dissolved-CO2 state at the end
  float gasRate;          // g/L/d, NAN = no decision
  float pressureStart;    // P1
  float pressureEnd;      // P2 as read
  float pressureEndIso;   // Dump: P2 after the polytropic correction
  float headSpaceBefore;
  float headSpaceAfter;
  float deltaH;           // Dump: headspace change applied (NAN = not applied)
  float beerVolume;       // after the task
  float dumpedVolume;     // batch total
  float pressureAfter[TASK_LOG_PRESSURE_SAMPLES]; // NAN = not collected
};

void doDataLog();
void doReliefDataLog(const ReliefLogData &data);
void doTaskDataLog(const TaskLogData &data);
const char *taskWindowTypeToText(byte type); // "Dump", "Gas", ... ("" = none)
void maybeSendBrewfatherLog();
void formatLocalEpochISO(uint32_t epoch, char *out, size_t outSize);

#endif  // POVOTO_DATALOG_H

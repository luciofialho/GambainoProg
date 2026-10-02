#ifndef POVOTO_DATALOG_H
#define POVOTO_DATALOG_H

#include <Arduino.h>

// One last Cold row and Brewfather point after entering Conditioning; after
// them nothing is sent while in Conditioning.
void requestConditioningFinalRecord();

// Snapshot captured after the post-relief pressure reading and its calculations.
// Keeping this separate from the live globals preserves values that are reset
// when the next relief cycle starts.
struct ReliefLogData {
  int povotoNumber;
  unsigned long reliefNumber;
  unsigned long valveOpenedMillis;
  unsigned long pressureReachedTargetMillis;
  unsigned long pressureAfterReliefMillis;
  float temperature;
  float targetPressure;
  float atmosphericPressure;
  float environmentTemperature;
  float tankReferenceTemperature; // expansion tank Tref, C (docs/expansion-tank-k.md)
  float reliefVolume;
  float effectiveVentingExponent;
  float pressureOnReliefMeasured;
  float currentOnReliefMeasured;
  float pressureReachedTarget;
  float pressureOnReliefExtrapolated;
  float pressureAfterRelief;
  float currentAfterRelief;
  float adjustedPressureAfterRelief;
  float adjustedEquilibriumPressure;
  float tankPressureAtClose; // expansion tank at valve close, gauge
  float liquidMassInGasVentingPercent;
  // Final residual of the previous tank cycle, sampled at this relief's opening.
  float expansionTankResidualMoles;
  float ventingElapsedAtLogSeconds;
  float previousTankPressureAtCloseBar;
  float previousTankPressureAtOpenBar;
  float previousTankEjectedMoles;
  float ejectedMolsBeforeLiquidCorrection;
  float instantaneousPressureDropFactor;
  float pressureDropFactor;
  float headSpaceVolume;
  float beerVolume;
  float ejectedMols;
  bool gasFlowModelActive;
  float gasOpeningSeconds;
  float gasPreviousVentingSeconds;
  float gasExpansionResidual;
  float gasTankMolesAtClose;
  float gasVentingResidualFactor;
  float gasVentingResidual;
  float gasExpansionOptimalSeconds;
  float gasPlannedExpansionSeconds;
  float gasCalculatedPressureCompensation;
  float gasAppliedPressureCompensation;
  const char *gasHeadspaceUpdateStatus;
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
  float headSpaceMeasured;   // this relief's instantaneous value (NAN = invalid)
  float kCO2FromBeerVolume;  // k matching FMTVolume - beer volume (NAN = no beer volume)
  float headSpaceEMA;        // headspaceFiltered after the update
  float headSpaceDaily;      // 24-hour average (NAN = no hours)
  uint8_t dailyHours;
  const char *dailyState;    // "valid", "hold" or "ema"
};

void doDataLog();
void doReliefDataLog(const ReliefLogData &data);
void maybeSendBrewfatherLog();
void formatLocalEpochISO(uint32_t epoch, char *out, size_t outSize);

#endif  // POVOTO_DATALOG_H

#ifndef POVOTO_DATALOG_H
#define POVOTO_DATALOG_H

#include <Arduino.h>

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
  float ejectedPressure;
  float liquidMassInGasVentingPercent;
  // Final residual of the previous tank cycle, sampled at this relief's opening.
  float expansionTankResidualMoles;
  float ventingElapsedAtLogSeconds;
  unsigned long previousReliefNumber;
  float previousTankPressureAtCloseBar;
  float previousTankMolesAtClose;
  float previousTankPressureAtOpenBar;
  float previousTankEjectedMolesBeforeLiquidCorrection;
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
  float gasInitialResidualMoles;
  float gasTankMolesAtClose;
  float gasModelTankMolesAtClose;
  // (model - measured) / measured * 100; positive means the model overestimated.
  float gasModelTankMolesDifferencePercent;
  float gasVentingResidualFactor;
  float gasVentingResidual;
  float gasExpansionOptimalSeconds;
  float gasVentingOptimalSeconds;
  float gasVentingFermenterOptimalSeconds;
  float gasVentingVolumeRatio;
  float gasPlannedExpansionSeconds;
  float gasPlannedVentingSeconds;
  float gasProjectedFermenterPressure;
  float gasProjectedExpansionPressure;
  float gasProjectedPressureDifference;
  float gasCalculatedPressureCompensation;
  float gasAppliedPressureCompensation;
  const char *gasHeadspaceUpdateStatus;
  double totalMolsEjected;
  float headSpaceCO2Mols;
  double dissolvedCO2Mols;
  double totalCO2Mols;
  float beerSG;
  float beerRealPlato;
  float beerABV;
  unsigned long totalReliefCount;
  float reliefsPerHour;
  float beerCO2EvolutionGramsPerLiterPerDay;
  unsigned long polytropicSourceReliefNumber;
  uint8_t polytropicSampleCount;
  float polytropicBackExtrapolatedPressure;
  float polytropicFitSlopeBarPerMinute;
  float polytropicEstimatedExponent;
  float polytropicFitRMSEBar;
};

void doDataLog();
void doReliefDataLog(const ReliefLogData &data);
void maybeSendBrewfatherLog();
void formatLocalEpochISO(uint32_t epoch, char *out, size_t outSize);

#endif  // POVOTO_DATALOG_H

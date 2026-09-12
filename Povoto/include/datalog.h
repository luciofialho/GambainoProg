#ifndef POVOTO_DATALOG_H
#define POVOTO_DATALOG_H

#include <Arduino.h>

// Snapshot captured after the post-relief pressure reading and its calculations.
// Keeping this separate from the live globals preserves values that are reset
// when the next relief cycle starts.
struct ReliefLogData {
  int povotoNumber;
  unsigned long valveOpenedMillis;
  unsigned long pressureReachedTargetMillis;
  unsigned long pressureAfterReliefMillis;
  bool volumeDeterminationActive;
  float temperature;
  float targetPressure;
  float atmosphericPressure;
  float reliefVolume;
  float effectiveVentingExponent;
  float pressureOnReliefMeasured;
  float currentOnReliefMeasured;
  float pressureReachedTarget;
  float pressureOnReliefExtrapolated;
  float pressureAfterRelief;
  float currentAfterRelief;
  float adjustedPressureAfterRelief;
  float instantaneousPressureDropFactor;
  float pressureDropFactor;
  float headSpaceVolume;
  float beerVolume;
  float ejectedMols;
  double totalMolsEjected;
  float headSpaceCO2Mols;
  double dissolvedCO2Mols;
  float dissolvedCO2MolsAtEquilibrium;
  double totalCO2Mols;
  float beerSG;
  float beerRealPlato;
  float beerABV;
  unsigned long totalReliefCount;
  float reliefsPerHour;
  float beerCO2EvolutionGramsPerLiterPerDay;
};

void doDataLog();
void doReliefDataLog(const ReliefLogData &data);
void maybeSendBrewfatherLog();

#endif  // POVOTO_DATALOG_H

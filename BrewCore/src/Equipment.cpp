#include "Equipment.h"
#include <Variables.h>
#include "Status.h"



//extern variables
float infusionVolume;
float HLTInfusionTemp = 0;
float HLTRampTemp=0;
float HLTDeltaTemp;
float rampInfusionVolume;

int kegSensorStabilityTime = 1000;


void calculateVolumesAndTemperatures(bool first) {
  // External temperature
  float EnvT = EnvironmentTemp;
  if (EnvT<=0 || EnvT==85)
      EnvT = 20;  // **** CONSTANTE ****
  
      /*
  float WIT = WaterInTemp;
  if (WIT<=0 || WIT==85)
      WIT = EnvT-2; // **** CONSTANTE *****
    */
  rampInfusionVolume = (float)RcpRampGrainWeight * (float)1.5 + MLTVOLUMELTOFALSEBOTTOM; /****CONSTANTE *****/
  infusionVolume     = (float)RcpGrainWeight * (float)RcpWaterGrainRatio + MLTVOLUMELTOFALSEBOTTOM - rampInfusionVolume;
  
  // thermical capacity of each element
  float HLTQq    = HLTWEIGHT*HLT_SPECIFICHEAT;
  float MLTQq    = MLT_SPECIFICHEAT*MLTWEIGHT;
  float HLTChillerQq = HLTCHILLERWATERVOLUME*WATER_SPECIFICHEAT + HLTCHILLERWEIGHT*HLTCHILLER_SPECIFICHEAT;
  float GrainQq;
  GrainQq = (float) RcpGrainWeight * GRAIN_SPECIFICHEAT;

  if (RcpRampGrainWeight == 0 || RcpRampTime==0) 
    HLTInfusionTemp = (RcpMashTemp * (MLTQq + GrainQq + WATER_SPECIFICHEAT * infusionVolume) - EnvT * (MLTQq + GrainQq + HLTChillerQq))
                      / (WATER_SPECIFICHEAT * (infusionVolume - HLTCHILLERWATERVOLUME));
  else {
    float GrainQq1 = (float) RcpRampGrainWeight * GRAIN_SPECIFICHEAT;
    HLTRampTemp = ((RcpRampTemp + HeatAdditiveCorrection)  * (MLTQq + GrainQq1 + WATER_SPECIFICHEAT * rampInfusionVolume) - EnvT * (MLTQq + GrainQq1 + HLTChillerQq))
                      / (WATER_SPECIFICHEAT * (rampInfusionVolume - HLTCHILLERWATERVOLUME));
    float rampT =RcpRampTemp - 2; /****Trocar pelo cálculo de perda ***/
    float GrainQq2 = (float) (RcpGrainWeight - RcpRampGrainWeight) * GRAIN_SPECIFICHEAT;
    float totalInfusionVol = rampInfusionVolume + infusionVolume;
    HLTInfusionTemp  = (
        RcpMashTemp * (MLTQq + GrainQq + WATER_SPECIFICHEAT*totalInfusionVol ) - (MLTQq + GrainQq1 + WATER_SPECIFICHEAT*rampInfusionVolume) * rampT - (GrainQq2 + WATER_SPECIFICHEAT*HLTCHILLERWATERVOLUME)*EnvT
    ) / (
        WATER_SPECIFICHEAT * (infusionVolume - HLTCHILLERWATERVOLUME)
    );
    
  }
    
  if (first)
      say("TEMPORARY INFUSION VOLUME AND TEMPERATURE CALCULATIONS:");
  else
      say("FINAL INFUSION VOLUME AND TEMPERATURE CALCULATIONS:");
      
  say();
  say("Considering");
  say("....Environment temperature             : %.1f C", EnvT);
  //say("....Water intake temperature            : %.1f C", WIT);
  say("....Grain weight                        : %.1f kg",float(RcpGrainWeight));
  say("....Infusion volume                     : %.1f l",infusionVolume+rampInfusionVolume);
  say("....Ramp time @ temperature             : %.1f min @ %.1f C",float(RcpRampTime),float(RcpRampTemp));
  say();
  
  say("Result:");
  if (RcpRampGrainWeight ==0) {
    say("....HLT temp for infusion    : %.1f c",HLTInfusionTemp);
  }
  else {
    say("....Ramp infusion volume        : %.1f l",rampInfusionVolume);
    say("....HLT temp for ramp infusion  : %.1f c",HLTRampTemp);
    say("....Second infusion volume      : %.1f l",infusionVolume);
    say("....HLT temp for second infusion: %.1f c",HLTInfusionTemp);
    say("....HLT temp delta after rest   : %.1f c",HLTDeltaTemp);
  }
  say();
}

//  **** HLT Mash Volume equation:
// consider the abreviations:
//   S = SPARGETEMP
//   P = HLTMASHOUTTEMP
//   I = WaterInTemp (WIT)
//   H = HLTQq   (HLTWEIGHT * HLT_SPECIFICHEAT) 
//   D = HLTDEADVOLUME
//   W = WATER_SPECIFICHEAT
//   M = HLTMAXVOLUME
//   X = variable to be calculated: the volume in HLT during mashout, so new water will bring HLT to sparge temp
// 
// The equation is as follows:
//      s(h+(m+d)(w))=p(h+(x+d)(w))+i(m−x)w
      
// Step 1: Flip the equation:
//      dpw+imw−iwx+pwx+hp=dsw+msw+hs
//      
// Step 2: Add -hp to both sides.
//      dpw+imw−iwx+pwx+hp+−hp=dsw+msw+hs+−hp
//      dpw+imw−iwx+pwx=dsw+msw−hp+hs
// 
// Step 3: Add -dpw to both sides.
//      dpw+imw−iwx+pwx+−dpw=dsw+msw−hp+hs+−dpw
//      imw−iwx+pwx=−dpw+dsw+msw−hp+hs
//      
// Step 4: Add -imw to both sides.
//      imw−iwx+pwx+−imw=−dpw+dsw+msw−hp+hs+−imw
//      −iwx+pwx=−dpw+dsw−imw+msw−hp+hs
//      
// Step 5: Factor out variable x.
//      x(−iw+pw)=−dpw+dsw−imw+msw−hp+hs
// 
// Step 6: Divide both sides by -iw+pw.
//      x(−iw+pw)         −dpw+dsw−imw+msw−hp+hs
//     -----------    =  ------------------------
//       −iw+pw                 −iw+pw
// 
//          −dpw+dsw−imw+msw−hp+hs
//     x = ------------------------
//                 −iw+pw
//                 
// Step 7: grouping:
// 
//          (h−dw) (s-p) + mw (s-i)
//     x = --------------------------
//                   w (p-i) 
// 
//      
// with the real names:
// 
//   = ((HLTQq - HLTDEADVOLUME*WATER_SPECIFICHEAT) * (SPARGETEMP - HLTMASHOUTTEMP) + HLTMAXVOLUME * WATER_SPECIFICHEAT * (SPARGETEMP - WIT))
//     /
//     (WATER_SPECIFICHEAT * (HLTMASHOUTTEMP-WIT);
// 
// (with MathPapa help)    
//   

float CIPLineTemperature() {
  return CIPLineTemperatures[CIPLineMode()];
}

float CIPLineRinseVolume() {
  if (Status >= CIPDRINSE1 && Status <= CIPDRINSE3)
    return CIP_Line_RinseVolumes[CIPLineMode()][int(Status)-CIPDRINSE1];
  else if (Status >= CIPPBRINSE1 && Status <= CIPPBRINSE3)
    return CIP_Line_RinseVolumes[CIPLineMode()][int(Status)-CIPPBRINSE1];
  else if (Status == CIPPBRINSE4)
    return CIP_Line_RinseVolumes[CIPLineMode()][2]; // same volume as phase 3
  return 0;
}

int CIPLineCirculationTime() {
  return CIPLineCirculationTimes[CIPLineMode()][int(Status)-CIPDCIRCULATION1];
}

float CIPLinePotSoakTime() {
  return CIPLinePotSoakTimes[int(ProgramParams) / 10 % 10];
}

byte CIPLineMode() {
  return int(ProgramParams) / 10 % 10;
};

bool CIPLineWithSoak() {
  return (int(ProgramParams) % 10) >=2; 
}; 
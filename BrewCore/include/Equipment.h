#ifndef Equipment_h
#define Equipment_h
#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include <Variables.h>
//------------------------------- FLAG CONSTANTS
#define CONSTANTHEAT 327


//------------------------------- POTS VOLUMETRY
#define MLTVOLUMELTOFALSEBOTTOM       15          // volume necessary to drown false bottom

#define HLTMAXVOLUME                 110          // deducts dead volume and safety head
#define HLTDEADVOLUME                  5.5 
#define HLTMINVOLUMEFORHEATER         15          // minimum volume in HLT to turn on heater
#define HLTMINVOLUMEFORTEMP           40          // used in SPARGE - bellow this, does not control temperature anymore 

#define BKMAXVOLUME                  110          // deducts dead volume and safety head
#define BKDEADVOLUME                   5          // dead volume considering normal valve B in brew configuration
#define BKVOLUMEFORVALVEA             30          // in liters
#define BKMINVOLUMEFORHEATER          18          // minimum volume in HLT to turn on heater
#define BKTYPICALBOILINGVOLUME       100          // just for GUI information

#define MLTWEIGHT                     26.          // in kg
#define MLT_SPECIFICHEAT               0.470      // in J/g ºC
#define HLTWEIGHT                     25          // in kg
#define HLT_SPECIFICHEAT               0.470      // in J/g ºC

#define HLTCHILLERWATERVOLUME          1          // liters of water retained inside HLT chiller
#define HLTCHILLERWEIGHT               3          // in kg
#define HLTCHILLER_SPECIFICHEAT        0.470      // in J/g ºC

#define COLDBANKVOLUME                 125        // liters
const float coldBankTargetTempCurve[]= {10,7,5,3,1}; //prebrew, 3 hours to begin brew,  end of hlt full load, firt run start, boil start
#define PREBREWCOLDBANKTARGETTEMP      7





//------------------------------- FMT CIP
#define FMTCIPSPRAYTIME               (30 * MINUTES)
#define FMTCIPVOL                     15 // liters
#define FMTCIPPURERINSECIRCULATIONTIME (5*MINUTES)
#define FMTCIPRINSECIRCULATIONTIME    (2*MINUTES)
#define FMTCIPNUMPURERINSESW          2
#define FMTCIPNUMDETERGRINSESW        5 
#define FMTDRAINTIME                  (60*SECONDS)

#define FMTDRAINTIMETOWARMWATER       (5*SECONDS)

#define KEGRINSECYCLES                 5
#define KEGRINSETIMEDURINGCLEANING     (60*SECONDS)
#define KEGRINSEONLYTIME               (180*SECONDS)
#define KEGRINSEVOLUME                 5
#define KEGCLEANVOLUME                 4
#define KEGCLEANTIME                   (10*MINUTES)
#define KEGCLEANERTRANSFEROUTMAXTIMERINSE (7*SECONDS)
#define KEGCLEANERTRANSFEROUTMAXTIMECLEAN (20*SECONDS)
#define KEGTRANSFERTOCLEANERMINTIME    (20*SECONDS)
#define KEGTRANSFERTOCLEANERMAXTIME    (300*SECONDS)
extern int kegSensorStabilityTime;

//------------------------------ valves and pumps facts
#define WAITFORVALVE                  (11 * SECONDS)
#define PUMPLATENCY                   (30 * SECONDS)
#define VOLUMETOHEATSERVICEWATER      5


//-------------------------------- BREW PROCESS
#define FINALMASHTEMPERATURE          69            // in last minutes of mashing process, the mash temperature will raise to a high alpha temperatute to save mashout temp 
#define MASHOUTTARGETTEMP             77            // target temperature for mashout
#define SPARGETEMP                    80            // HLT water temperature for sparging
#define MAXWORTTEMPINCOILFORMASHING   72            // maximum wort temperature in coil during mashing
#define MAXWORTTEMPINCOILFORMASHOUT   81            // maximum wort temperature in coil during mashout
#define ALLOWEDSPARGETEMPDELTA         3            // +- n degrees to allow sparging. outside the range SPARGETEMP +- this value, sparge will pause
#define PREBOILTEMP                   96            // temperature to keep wort during transfer to BK
#define CARAMELIZATIONBOILSTARTTEMP   96.1          // temperature to be considered as beginning of caramelization boil
#define CARAMERILIZBOILEVAPORATION     0.7          // in fraction of wort evaporated per minute (eg 1 means 1% will evaparote every minute
#define SUBBOILRAMPTIME              (10.0*MINUTES) // sub boil approaches recipe temperature in a ramp - this the ramp time
#define SUBBOILRAMPDELTA               1            // sub boil approaches recipe temperature in a ramp - this the temp temperature delta 
#define VOLUMETOCHECKHLTEMPTY          5            // interprets flow interruption as dry HLT only if counts less than this number of liters. Otherwise, interprets it as equipment failure

//-------------------------------- LINE RINSE (CIPW)
const float CIPWVolumes[2][4] = 
  {{10, 10, 10, 12},           // Water CIP volumes - full cycle 
   { 5,  5,  5, 5}};           // Water CIP volumes - auto cycle

#define CIPHOTWATERVOLUME             65            // hot water volume for pre-brew CIP 
#define CIPHOTWATERTEMP               CONSTANTHEAT  // temperature for pre-brew CIP hot water
#define CIPHOTWATERCIRCULATIONSTART   86            // celsius to start circulation during pre-brew CIP with hot water
#define CIPHOTWATERSTARTTEMP          90            // celsius to start counting time in pre-brew CIP with hot water 
#define CIPHOTWATERTIME_1             (15.0*MINUTES)
#define CIPHOTWATERTIME_2             (15.0*MINUTES)

#define PREBREWCIP_FMTSANITIZER       (30.0*MINUTES) // time to spray sanitizer in FMT during pre-brew CIP 


//------------------------------- LINE CIP (CIPD / PBCIP / BOILING WATER CIP)
#define CIPDVOLUME                    28            // water volume in pot to detergent CIP circulation
const float CIP_Line_RinseVolumes[3][3] = 
  { {16, 10, 4},    // post-brew rinse volumes
    {100,70,40},    // detergent CIP rinse volumes - alkaline
    {60, 40, 30}};  // detergent CIP rinse volumes - acid

const float CIPLineCirculationTimes[3][3] =
  { {15.0*MINUTES, 15.0*MINUTES, 15.0*MINUTES},  // Boiling water CIP times
    { 8.0*MINUTES, 10.0*MINUTES,  2.0*MINUTES},  // detergent CIP times - alkaline
    { 6.0*MINUTES,  7.0*MINUTES,  2.0*MINUTES}}; // detergent CIP times - acid

const float CIPLineTemperatures[3] =
  { CONSTANTHEAT,  // boiling water CIP temperature
            60.0,   // detergent CIP temperature - alkaline
            35.0};  // detergent CIP temperature - acid

const float CIPLinePotSoakTemperatures[3] =
  {     0,  // N/A
     70.0,   // detergent soak temperature - alkaline
     35.0};  // detergent soak temperature - acid

const float CIPLinePotSoakTimes[3] =
  {      0,          // N/A
     30.0*MINUTES,   // detergent soak time - alkaline
     15.0*MINUTES};  // detergent soak time - acid


#define KEGGINGWATERVOLUME            65
#define KEGGINGWATERBOILTIME          (20*60)

//-------------------------------- BREWWARE ASSUMPTIONS
#define ALLOWEDTEMPOFFSET               0.3        // allowed temperature oscilation when controling heaters

#define TRANSFERTEMPOFFSET              1          // Allowed temperature oscillation during first phase of transfer to fermenter
#define INOCULATIONTEMPALLOWEDOFFSET    0.1        // Allowed temperature oscillation in unfermented beer in fermenter during second phase of transfer to fermenter
#define WHIRLPOOLDELTATEMP              1.0        // will stop whirlpool when reach target temperature + X, because of hysteresis


// Phisical constants
#define LODOWATERTEMPERATURE           90.0   // temperature to remove dissolved oxygen from water
#define TIMETODEAREATE                 (5*MINUTES) // time to deaerate water at LODOWATERTEMPERATURE
#define WATERBOILTEMP                  97.5   // water temperature to be considered boil in standby flag - consider altitude
#define WORTBOILTEMP                   97.6   // water temperature to be considered boil in standby flag - consider altitude
#define WORTTEMPTOAVOIDBOILDOVER       97.4   // wort temperature to avoid boil over


#define INFUSIONTEMPLOSS                2          // in percent

#define WATER_SPECIFICHEAT              4.18       // j/g oC
#define GRAIN_SPECIFICHEAT              1.7        // j/g oC

#define RAMPRESTCOEFICIENT              0.003      // oC / minute / deltaTemp  example: rest at 50C for 30 minutes in enviroment = 20C we loss 2 degrees in MLT, so 2/(50-30)/30 = 0.003




// DRYER CONTROL
#define HOTSIDEDRYERTARGETTEMP 50
#define HOTSIDEDRYERTEMPOFFSET 5


// DIAGNOSTIC PARAMETES
#define FLOWREADINGSTABILIZATIONTIME (5*SECONDS) // used in checks for valves
#define LONGFLOWREADINGSTABILIZATIONTIME (30*SECONDS) // used to diagnose meters and pumps
#define NOFLOWREADING (0.1) // in liters per minute
#define POSITIVEFLOWREADING (1) // in liters per minute

extern float infusionVolume;
extern float HLTInfusionTemp;
extern float HLTRampTemp;
extern float HLTDeltaTemp;
extern float rampInfusionVolume;

void calculateVolumesAndTemperatures(bool first);

byte CIPLineMode();     // 0=boiling water, 1=alkaline detergent, 2=acid detergent
bool CIPLineWithSoak(); 

int CIPLineTimePhase();
int CIPLineCirculationTime();
float CIPLineTemperature();
float CIPLineRinseVolume();
float CIPLinePotSoakTime();



#endif



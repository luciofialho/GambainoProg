#ifndef Variables_h

#define Variables_h
#include "pins.h"
#include "ProcVar.h"

void configVariables();

void processDerivedVars();

void quickI2CSetZeros();

extern unsigned long int lastPressureReceived;


// control
extern ProcVar 
  Power12VOn,
  Power12VBlock,
  OnGoingProgram, 
  ProgramParams,           
  Status,         
  SubStatus,
  IPCStatus,           
  EndStatus,                 
  SkipToNextStatus,          
  CirculationMode,           
  LineConfiguration,         
  TimeInStatus,              
  IPCTimeInStatus,           
  Led1,
  Led2,
  Buzzer,

// recipe
  RcpGrainWeight,            
  RcpMashTime,               
  RcpMashTemp,               
  RcpWaterGrainRatio,        
  RcpRampGrainWeight,        
  RcpRampTime,               
  RcpRampTemp,               
  RcpCaramelizationBoil,     
  RcpBoilTime,               
  RcpBoilTemp,               
  RcpWhirlpoolTerminalTemp,  
  RcpWhirlpoolHoppingTime,   
  RcpWhirlpoolHoppingTemp,   
  RcpWhirlpoolMinimumTime,   
  RcpWhirlpoolRestTime,      
  RcpInoculationTemp,        
  RcpFirstWortHopping,       
  RcpAddition1,              
  RcpAddition2,              
  RcpAddition3,              
  RcpAddition4,              
  RcpAddition5,
  RcpFermenterCleaning,
  RcpTargetFermenter,      
  RcpTopupWater,  
  RcpKettleSourFirstBoilTime,
  RcpKettleSourSouringTemp,  
  RcpStartTime,        
  RcpBatchNum,
  FMT1BatchNum,
  FMT2BatchNum,

// level and pressure control
  MLTTopLevel,               
  HLTVolume,                 
  HLTTargetVolume,   
  HLTLevel,        
  BKLevel,                
  TransferVolume,
  TransferFlow,   
  HLT2MLTFlow,

// Water Control
  HotWaterIn,
  ColdWaterIn,
  WaterInTarget,
  WaterInFlow,
  WaterInETA,
  DrainBlock,

// heater control
  HLTHeater,                 
  BKHeater,                  
  BKHeaterL,                 
  BKHeaterH,  
  TopUpHeater,
  Dehumidifier,               

// Hot side valves 
  HLTValve,                  
  HLTWaterIn, 
  HLTDrain,

  MLTValveA,                 
  MLTValveB,                 
  MLTDrain,

  BKValveA,                  
  BKValveB,                  
  WhirlpoolValve,            
  BKWaterIn,  
  BKDrain,

// Hot side pumps
  MLTPump,                   
  HLTPump,                   
  BKPump,                    
  

// Hot side temperature
  HLTTargetTemp,             
  HLTTemp,
  HLTOutTemp,  
  MLTTopTemp,                
  MLTTemp,                   
  MLTOutTemp,             
  MLTTargetTemp,             
  BKTargetTemp,              
  BKTemp,            
  TopUpWaterTemp,        
  FermenterTemp,             
  EnvironmentTemp,           

// Mash temperature control 
  HeatAdditiveCorrection,
  HeatDampeningFactor,
  HeatCoilEfficiency,

// Chillers
  Chiller1,             
  ColdBankPump,              
  Chiller1Temp,         
  Chiller2Temp,         
  Chiller2WasteTemp,
  TransferAvgTemp,
  ColdBankWaterIn,                
  ColdBankTemp,              
  ColdBankTargetTemp,        
  ColdBankControl,           
  ColdBankValve,

// KEG CIP
  KegPump,
  KegWaterIn,
  KegDetergentValve,
  KegDetergentPump,
  KegDrain,
  KegCycle,
  KegLiquidSensor,

// FMT CIP
  FMTWaterIn,
  FMTPump,                    
  FMTDrain, 
  FMTCycle,

// Pot cleaner
  PotCleanerDrain,
  PotCleanerWaterIn;


#endif



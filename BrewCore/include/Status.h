#ifndef status_h
#define status_h

#include "Equipment.h"

#define STATUSNAMESIZE 25
#define DEFSTATUS(NUM,NAME,STR) const int NAME = NUM; const char StatusName_##NUM [STATUSNAMESIZE] = STR;

DEFSTATUS ( 0, STANDBY                      ,"Standby")

DEFSTATUS ( 1, MANUALCONTROL                ,"Manual Control")
DEFSTATUS ( 2, PREBREWSETUP                 ,"Setup for prebrew CIP")
DEFSTATUS ( 3, PREBREWHOTWATERPREP          ,"Hot water CIP prep")
DEFSTATUS ( 4, PREBREWHOTWATERCIRC1         ,"Hot water CIP circuit 1")
DEFSTATUS ( 5, PREBREWHOTWATERCIRC2         ,"Hot water CIP circuit 2")

DEFSTATUS ( 6, BREWWAIT                     ,"Brew setup / countdown")
DEFSTATUS ( 7, HLTLOADANDDEAERATE           ,"HLT load and deaerate")  
DEFSTATUS ( 8, PREPWATERFORINFUSION         ,"Prep water for infusion")
DEFSTATUS ( 9, RAMPINFUSION                 ,"Infusion for ramp")  
DEFSTATUS (10, RAMPREST                     ,"Ramp rest")  
DEFSTATUS (11, INFUSION                     ,"Infusion")  
DEFSTATUS (12, MASHGRAINREST                ,"Grain bed settlement")  
DEFSTATUS (13, MASHMASHING                  ,"Mashing") 
DEFSTATUS (14, MASHFIRSTRUN                 ,"First run")
DEFSTATUS (15, MASHMASHOUT                  ,"Mashout")
DEFSTATUS (16, PREPSPARGE                   ,"Prepare sparge water")
DEFSTATUS (17, SPARGE                       ,"Fly sparge")  

DEFSTATUS (18, KS_WAITFIRSTBOIL             ,"KS: Wait first boil")
DEFSTATUS (19, KS_FIRSTBOIL                 ,"KS: first boiling")
DEFSTATUS (20, KS_WHIRLPOOL                 ,"KS: first whirlpool")
DEFSTATUS (21, KS_SOURINGSETUP              ,"KS: souring setup")
DEFSTATUS (22, KS_CLEAN1                    ,"KS: clean 1")
DEFSTATUS (23, KS_CLEAN2                    ,"KS: clean 2")
DEFSTATUS (24, KS_SOURING                   ,"KS: souring rest")

DEFSTATUS (25, WAITBOIL                     ,"Waiting boil")
DEFSTATUS (26, BOIL                         ,"Boiling")
DEFSTATUS (27, WHIRLPOOLCHILLTOHOP          ,"Whirlpool chill to hop") 
DEFSTATUS (28, WHIRLPOOLHOPPING             ,"Whirlpool hopping")
DEFSTATUS (29, WHIRLPOOL                    ,"Whirlpool")
DEFSTATUS (30, WHIRLPOOLREST                ,"Whirlpool rest")
DEFSTATUS (31, TRANSFER                     ,"Transf to fermenter")

DEFSTATUS (32, CIPPBSETUP                   ,"Postbrew rinse Setup")
DEFSTATUS (33, CIPPBRINSE1                  ,"Postbrew rinse phase 1/4")  
DEFSTATUS (34, CIPPBRINSE2                  ,"Postbrew rinse phase 2/4")
DEFSTATUS (35, CIPPBRINSE3                  ,"Postbrew rinse phase 3/4")
DEFSTATUS (36, CIPPBRINSE4                  ,"Postbrew rinse phase 4/4")

DEFSTATUS (37, CIPDSETUP                    ,"Deterg CIP setup")
DEFSTATUS (38, CIPDLOADHEAT                 ,"Deterg CIP load/heat")
DEFSTATUS (39, CIPDCIRCULATION1             ,"Deterg CIP phase 1/3")
DEFSTATUS (40, CIPDCIRCULATION2             ,"Deterg CIP phase 2/3")
DEFSTATUS (41, CIPDCIRCULATION3             ,"Deterg CIP phase 3/3")
DEFSTATUS (42, CIPDRINSE1                   ,"Deterg CIP rinse 1/3")
DEFSTATUS (43, CIPDRINSE2                   ,"Deterg CIP rinse 2/3")
DEFSTATUS (44, CIPDRINSE3                   ,"Deterg CIP rinse 3/3")
DEFSTATUS (45, CIPDCHECKRINSE               ,"Deterg CIP check rinse")

DEFSTATUS (46, CIPWSTART                    ,"Rinse setup")
DEFSTATUS (47, CIPWRINSE1                   ,"Rinse phase 1 / 4")
DEFSTATUS (48, CIPWRINSE2                   ,"Rinse phase 2 / 4") 
DEFSTATUS (49, CIPWRINSE3                   ,"Rinse phase 3 / 4")
DEFSTATUS (50, CIPWRINSE4                   ,"Rinse phase 4 / 4")

// incluir status CIPWAFTERRINSE
DEFSTATUS (51, CIPWAFTERRINSE               ,"Rinse after rinse")

DEFSTATUS (52, MANUALCIPLINE                ,"Manual CIP - Line")
DEFSTATUS (53, MANUALCIPFMT                 ,"Manual CIP - FMT")

DEFSTATUS (54, CIPFMTSTART                  ,"FMT: Start")
DEFSTATUS (55, CIPFMTFIRSTOPENRINSE         ,"FMT: First rinse")
DEFSTATUS (56, CIPFMTMANUALCLEAN            ,"FMT: Manual clean")
DEFSTATUS (57, CIPFMTSERVICEWATERRINSE      ,"FMT: Svc water rinse")
DEFSTATUS (58, CIPFMTDETERGENTCIRC          ,"FMT: Detergent circ")
DEFSTATUS (59, CIPFMTCHECKRINSE             ,"FMT: Check rinse")
DEFSTATUS (60, CIPFMTCOMPLETE               ,"FMT: Complete")
DEFSTATUS (61, KEGCLEANERSETUP              ,"Keg: setup")
DEFSTATUS (62, KEGCLEANERRINSE              ,"Keg: cleaner rinse")
DEFSTATUS (63, KEGPREPDETERG                ,"Keg: prep detergent")
DEFSTATUS (64, KEGDETERGSPRAY               ,"Keg: detergent spray")
DEFSTATUS (65, KEGSAVEDETERGENT             ,"Keg: save detergent")
DEFSTATUS (66, KEGRINSE                     ,"Keg: rinse")
DEFSTATUS (67, KEGRETURNDETERGENT           ,"Keg: return detergent")
DEFSTATUS (68, KEGASKANOTHERKEG             ,"Keg: do another keg?")
DEFSTATUS (69, KEGDETERGENTDRAIN            ,"Keg: detergent drain")
DEFSTATUS (70, KEGCLEANERFINALRINSE         ,"Keg: cleaner final rinse")

DEFSTATUS (71, DIAGI2C                      ,"Diag: I2C buses")
DEFSTATUS (72, DIAGTHERMOMETERS             ,"Diag: Thermometers")
DEFSTATUS (73, DIAGMOTORVALVESCOMMAND       ,"Diag: valves command")
DEFSTATUS (74, DIAGVALVES1                  ,"Diag: Valves 1")
DEFSTATUS (75, DIAGVALVES2                  ,"Diag: Valves 2")
DEFSTATUS (76, DIAGVALVES3                  ,"Diag: Valves 3")
DEFSTATUS (77, DIAGVALVESFMT                ,"Diag: Valves FMT")
DEFSTATUS (78, DIAGPUMPSFLOWMETERS          ,"Diag: Pumps and flow")
DEFSTATUS (79, DIAGMLTLEVEL                 ,"Diag: MLT Level Sensors")
DEFSTATUS (80, DIAGBKLEVEL                  ,"Diag: BK Level Sensors")
DEFSTATUS (81, DIAGRESULT                   ,"Diag: Result")

DEFSTATUS (82, CALIBRATEFLOWSTART           ,"Calbr flow: start")
DEFSTATUS (83, CALIBRATEFLOWSTABILIZATION   ,"Calbr flow: stabilizate")
DEFSTATUS (84, CALIBRATEFLOWMEASUREMENT     ,"Calbr flow: measurement")
DEFSTATUS (85, CALIBRATEFLOWEND             ,"Calbr flow: end")

#define NUMSTATUS                           86


#define PGMSTANDBY          0
#define PGMMANUALCONTROL    1
#define PGMBREW             2
#define PGMCIPLINE          3
#define PGMNA4            4
#define PGMNA5          5
#define PGMCIPRINSE         6
#define PGMCIPAUTORINSE     7
#define PGMCIPMANUAL        8
#define PGMFMTMANUAL        9
#define PGMFMTAUTO1PHASE   10
#define PGMFMTAUTO2PHASE   11
#define PGMKEGCLEAN        12 // 12xy = x=1 full clean, x=2 rinse only; y=0 setup, y=1 no setup
#define PGMNA1             13
#define PGMNA2             14
#define PGMNA3             15
#define PGMDIAGMANUAL      16
#define PGMDIAGAUTO        17
#define PGMCALIBRFLOWCOLD1 18
#define PGMCALIBRFLOWCOLD2 19
#define PGMCALIBRFLOWHOT   20
#define PGMCALIBRFLOWPUMPS 21

#define NUMPROGRAMS 22

const byte pgmStatusRange [NUMPROGRAMS][2] = {
  {STANDBY,STANDBY},                        //0 standby
  {MANUALCONTROL,MANUALCONTROL},            //1
  {PREBREWSETUP ,CIPPBRINSE4},              //2
  {CIPDSETUP,CIPDCHECKRINSE},               //3
  {0,0},               //4
  {0,0},               //5
  {CIPWSTART,CIPWAFTERRINSE},               //6
  {CIPWSTART,CIPWAFTERRINSE},               //7  
  {MANUALCIPLINE,MANUALCIPLINE},            //8
  {MANUALCIPFMT,MANUALCIPFMT},              //9
  {CIPFMTSTART,CIPFMTCOMPLETE},             //10 
  {CIPFMTSTART,CIPFMTCOMPLETE},             //11  
  {KEGCLEANERSETUP,KEGCLEANERFINALRINSE},   //12
  {0,0},   //13
  {0,0},   //14
  {0,0},   //15
  {DIAGI2C,DIAGRESULT},                     //16
  {DIAGI2C,DIAGPUMPSFLOWMETERS},            //17
  {CALIBRATEFLOWSTART,CALIBRATEFLOWEND},    //18
  {CALIBRATEFLOWSTART,CALIBRATEFLOWEND},    //19
  {CALIBRATEFLOWSTART,CALIBRATEFLOWEND},    //20
  {CALIBRATEFLOWSTART,CALIBRATEFLOWEND}     //21  
};

const char * const statusNames[NUMSTATUS] = {
  StatusName_0,
  StatusName_1,
  StatusName_2,
  StatusName_3,
  StatusName_4,
  StatusName_5,
  StatusName_6,
  StatusName_7,
  StatusName_8,
  StatusName_9,
  StatusName_10,
  StatusName_11,
  StatusName_12,
  StatusName_13,
  StatusName_14,
  StatusName_15,
  StatusName_16,
  StatusName_17,
  StatusName_18,
  StatusName_19,
  StatusName_20,
  StatusName_21,
  StatusName_22,
  StatusName_23,
  StatusName_24,
  StatusName_25,
  StatusName_26,
  StatusName_27,
  StatusName_28,
  StatusName_29,
  StatusName_30,
  StatusName_31,
  StatusName_32,
  StatusName_33,
  StatusName_34,
  StatusName_35,
  StatusName_36,
  StatusName_37,
  StatusName_38,
  StatusName_39,
  StatusName_40,
  StatusName_41,
  StatusName_42,
  StatusName_43,
  StatusName_44,
  StatusName_45,
  StatusName_46,
  StatusName_47,
  StatusName_48,
  StatusName_49,
  StatusName_50,
  StatusName_51,
  StatusName_52,
  StatusName_53,
  StatusName_54,
  StatusName_55,
  StatusName_56,
  StatusName_57,
  StatusName_58,
  StatusName_59,
  StatusName_60,
  StatusName_61,
  StatusName_62,
  StatusName_63,
  StatusName_64,
  StatusName_65,
  StatusName_66,
  StatusName_67,
  StatusName_68,
  StatusName_69,
  StatusName_70,
  StatusName_71,
  StatusName_72,
  StatusName_73,
  StatusName_74,
  StatusName_75,
  StatusName_76,
  StatusName_77,
  StatusName_78,
  StatusName_79,
  StatusName_80,
  StatusName_81,
  StatusName_82,
  StatusName_83,
  StatusName_84,
  StatusName_85
};


extern bool restoringState;
extern char SubStatusLabel[80];

extern int programToStart;
extern int diagParameters;

void startProgram(int pgm);

bool skipStatus(int s=0);

void MainStateMachine();

void setSubStatus(byte subStatus, const char *subStatusLabel, ...);

#endif


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
DEFSTATUS ( 7, PREPWATERFORINFUSION         ,"Prep water for infusion")
DEFSTATUS ( 8, RAMPINFUSION                 ,"Infusion for ramp")  
DEFSTATUS ( 9, RAMPREST                     ,"Ramp rest")  
DEFSTATUS (10, INFUSION                     ,"Infusion")  
DEFSTATUS (11, MASHGRAINREST                ,"Grain bed settlement")  
DEFSTATUS (12, MASHMASHING                  ,"Mashing") 
DEFSTATUS (13, MASHFIRSTRUN                 ,"First run")
DEFSTATUS (14, MASHMASHOUT                  ,"Mashout")
DEFSTATUS (15, PREPSPARGE                   ,"Prepare sparge water")
DEFSTATUS (16, SPARGE                       ,"Fly sparge")  

DEFSTATUS (17, KS_WAITFIRSTBOIL             ,"KS: Wait first boil")
DEFSTATUS (18, KS_FIRSTBOIL                 ,"KS: first boiling")
DEFSTATUS (19, KS_WHIRLPOOL                 ,"KS: first whirlpool")
DEFSTATUS (20, KS_SOURINGSETUP              ,"KS: souring setup")
DEFSTATUS (21, KS_CLEAN1                    ,"KS: clean 1")
DEFSTATUS (22, KS_CLEAN2                    ,"KS: clean 2")
DEFSTATUS (23, KS_SOURING                   ,"KS: souring rest")

DEFSTATUS (24, WAITBOIL                     ,"Waiting boil")
DEFSTATUS (25, BOIL                         ,"Boiling")
DEFSTATUS (26, WHIRLPOOLCHILLTOHOP          ,"Whirlpool chill to hop") 
DEFSTATUS (27, WHIRLPOOLHOPPING             ,"Whirlpool hopping")
DEFSTATUS (28, WHIRLPOOL                    ,"Whirlpool")
DEFSTATUS (29, WHIRLPOOLREST                ,"Whirlpool rest")
DEFSTATUS (30, TRANSFER                     ,"Transf to fermenter")

DEFSTATUS (31, CIPPBSETUP                   ,"Postbrew rinse Setup")
DEFSTATUS (32, CIPPBRINSE1                  ,"Postbrew rinse phase 1/4")  
DEFSTATUS (33, CIPPBRINSE2                  ,"Postbrew rinse phase 2/4")
DEFSTATUS (34, CIPPBRINSE3                  ,"Postbrew rinse phase 3/4")
DEFSTATUS (35, CIPPBRINSE4                  ,"Postbrew rinse phase 4/4")

DEFSTATUS (36, CIPDSETUP                    ,"Deterg CIP setup")
DEFSTATUS (37, CIPDLOADHEAT                 ,"Deterg CIP load/heat")
DEFSTATUS (38, CIPDCIRCULATION1             ,"Deterg CIP phase 1/3")
DEFSTATUS (39, CIPDCIRCULATION2             ,"Deterg CIP phase 2/3")
DEFSTATUS (40, CIPDCIRCULATION3             ,"Deterg CIP phase 3/3")
DEFSTATUS (41, CIPDRINSE1                   ,"Deterg CIP rinse 1/3")
DEFSTATUS (42, CIPDRINSE2                   ,"Deterg CIP rinse 2/3")
DEFSTATUS (43, CIPDRINSE3                   ,"Deterg CIP rinse 3/3")
DEFSTATUS (44, CIPDCHECKRINSE               ,"Deterg CIP check rinse")

DEFSTATUS (45, CIPWSTART                    ,"Rinse setup")
DEFSTATUS (46, CIPWRINSE1                   ,"Rinse phase 1 / 4")
DEFSTATUS (47, CIPWRINSE2                   ,"Rinse phase 2 / 4") 
DEFSTATUS (48, CIPWRINSE3                   ,"Rinse phase 3 / 4")
DEFSTATUS (49, CIPWRINSE4                   ,"Rinse phase 4 / 4")

// incluir status CIPWAFTERRINSE
DEFSTATUS (50, CIPWAFTERRINSE               ,"Rinse after rinse")

DEFSTATUS (51, MANUALCIPLINE                ,"Manual CIP - Line")
DEFSTATUS (52, MANUALCIPFMT                 ,"Manual CIP - FMT")

DEFSTATUS (53, CIPFMTSTART                  ,"FMT: Start")
DEFSTATUS (54, CIPFMTFIRSTOPENRINSE         ,"FMT: First rinse")
DEFSTATUS (55, CIPFMTMANUALCLEAN            ,"FMT: Manual clean")
DEFSTATUS (56, CIPFMTSERVICEWATERRINSE      ,"FMT: Svc water rinse")
DEFSTATUS (57, CIPFMTDETERGENTCIRC          ,"FMT: Detergent circ")
DEFSTATUS (58, CIPFMTCHECKRINSE             ,"FMT: Check rinse")
DEFSTATUS (59, CIPFMTCOMPLETE               ,"FMT: Complete")
DEFSTATUS (60, KEGCLEANERSETUP              ,"Keg: setup")
DEFSTATUS (61, KEGCLEANERRINSE              ,"Keg: cleaner rinse")
DEFSTATUS (62, KEGPREPDETERG                ,"Keg: prep detergent")
DEFSTATUS (63, KEGDETERGSPRAY               ,"Keg: detergent spray")
DEFSTATUS (64, KEGSAVEDETERGENT             ,"Keg: save detergent")
DEFSTATUS (65, KEGRINSE                     ,"Keg: rinse")
DEFSTATUS (66, KEGRETURNDETERGENT           ,"Keg: return detergent")
DEFSTATUS (67, KEGASKANOTHERKEG             ,"Keg: do another keg?")
DEFSTATUS (68, KEGDETERGENTDRAIN            ,"Keg: detergent drain")
DEFSTATUS (69, KEGCLEANERFINALRINSE         ,"Keg: cleaner final rinse")

DEFSTATUS (70, DIAGI2C                      ,"Diag: I2C buses")
DEFSTATUS (71, DIAGTHERMOMETERS             ,"Diag: Thermometers")
DEFSTATUS (72, DIAGMOTORVALVESCOMMAND       ,"Diag: valves command")
DEFSTATUS (73, DIAGVALVES1                  ,"Diag: Valves 1")
DEFSTATUS (74, DIAGVALVES2                  ,"Diag: Valves 2")
DEFSTATUS (75, DIAGVALVES3                  ,"Diag: Valves 3")
DEFSTATUS (76, DIAGVALVESFMT                ,"Diag: Valves FMT")
DEFSTATUS (77, DIAGPUMPSFLOWMETERS          ,"Diag: Pumps and flow")
DEFSTATUS (78, DIAGMLTLEVEL                 ,"Diag: MLT Level Sensors")
DEFSTATUS (79, DIAGBKLEVEL                  ,"Diag: BK Level Sensors")
DEFSTATUS (80, DIAGRESULT                   ,"Diag: Result")

DEFSTATUS (81, CALIBRATEFLOWSTART           ,"Calbr flow: start")
DEFSTATUS (82, CALIBRATEFLOWSTABILIZATION   ,"Calbr flow: stabilizate")
DEFSTATUS (83, CALIBRATEFLOWMEASUREMENT     ,"Calbr flow: measurement")
DEFSTATUS (84, CALIBRATEFLOWEND             ,"Calbr flow: end")

#define NUMSTATUS                           85


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
  StatusName_84
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


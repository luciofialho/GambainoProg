#ifndef __ipc_h
#define __ipc_h

#define IPCSTATUSNAMESIZE 30
#define DEFIPCSTATUS(NUM,NAME,STR) const short NAME = NUM; const char IPCStatusName_##NUM [IPCSTATUSNAMESIZE]  = STR;

DEFIPCSTATUS ( 0, IPCNOTSTARTED             ,"---")
DEFIPCSTATUS ( 1, IPCPREBREWFMTDETERG       ,"FMT detergent clean")
DEFIPCSTATUS ( 2, IPCPREBREWWATERFLUSH      ,"FMT water flush")
DEFIPCSTATUS ( 3, IPCPREBREWFMTRINSE1       ,"FMT rinse 1/3 (hot)")
DEFIPCSTATUS ( 4, IPCPREBREWFMTRINSE2       ,"FMT rinse 2/3 (hot)")
DEFIPCSTATUS ( 5, IPCPREBREWFMTRINSE3       ,"FMT rinse 3/3 (cold)")
DEFIPCSTATUS ( 6, IPCPREBREWPREPSANIT       ,"FMT prep sanit")
DEFIPCSTATUS ( 7, IPCPREBREWLOADCOLDBANK    ,"FMT load cold bank")
DEFIPCSTATUS ( 8, IPCWAITBREWSTART          ,"Waiting brew start") 
DEFIPCSTATUS ( 9, IPCPREBREWSPRAYSANIT      ,"FMT Spraying sanitizer")
DEFIPCSTATUS (10, IPCPAUSEBEFORESANITLINE   ,"Pause before sanitizing line")
DEFIPCSTATUS (11, IPCSANITTOLINE            ,"Transfer sanitizer to line")
DEFIPCSTATUS (12, IPCSANITINLINE            ,"Sanitize line")
DEFIPCSTATUS (13, IPCPURGELINE              ,"Purge line")
DEFIPCSTATUS (14, IPCCLEANBK                ,"Clean BK")
DEFIPCSTATUS (15, IPCFINISHED               ,"---")

#define NUMIPCSTATUS 16

const char * const IPCStatusNames[NUMIPCSTATUS] = {
IPCStatusName_0,
IPCStatusName_1,
IPCStatusName_2,
IPCStatusName_3,
IPCStatusName_4,
IPCStatusName_5,
IPCStatusName_6,
IPCStatusName_7,
IPCStatusName_8,
IPCStatusName_9,
IPCStatusName_10,
IPCStatusName_11,
IPCStatusName_12,
IPCStatusName_13,
IPCStatusName_14,
IPCStatusName_15
};

extern long int IPCStatusEntryTime; 

void IPCStatusMachine();

void IPCStart();

#endif


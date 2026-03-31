#ifndef Todo_h
#define Todo_h

#include <Arduino.h>

#define MAXTODOS 50
#define ACTIONLABELLENGTH 50

#define LINEPREBREW               1
#define LINEINFUSION              2
#define LINEBREW                  3
#define LINEBREWTOPUP             4
#define LINERINSE                 5
#define LINEDETERG                6
#define LINEFMTCIP                7
#define LINECALIBRATION           8
#define NUMLINECONFIGS            9


class Todo {
    public:
        Todo(const char *Action);
        void start(const char *suffixStr=NULL,int secondsFromNow=0);
        void start(int secondsFromNow) {start(NULL,secondsFromNow);};
        bool notStarted();
        void dismiss();
        void reset();
        bool active();
        bool TodoIsReady();
        bool neededTodoIsReady();
        char * getJSON(char *buf);
        char status();

        static void monitorTodos();
        static Todo *getByIndex(byte idx);
        static int numTodos();
        static void resetTodos();
    private:
        byte idx;
        char actionLabel[ACTIONLABELLENGTH+1];
        long int secondsStart;
        long int secondsDismiss;
        bool isOverdue;
        const char *suffixStr_;
};

extern Todo Todo_StartBrew;
extern Todo Todo_Sanitizer;
extern Todo Todo_CleanBK;
extern Todo Todo_PurgeLine;
extern Todo Todo_MashRest;
extern Todo Todo_RunOffComplete;
extern Todo Todo_FirstWortHopping;
extern Todo Todo_PrepareTopUpPot;
extern Todo Todo_Addition1;
extern Todo Todo_Addition2;
extern Todo Todo_Addition3;
extern Todo Todo_Addition4;
extern Todo Todo_Addition5;
extern Todo Todo_WhirlpoolHopAddition;
extern Todo Todo_AllowTransferStart;
extern Todo Todo_TurnTransferPumpOn;
extern Todo Todo_SwitchBKValves;
extern Todo Todo_FinishTransfer;
extern Todo Todo_ReportTransferEnd;
extern Todo Todo_FinishTopUpWater;

extern Todo Todo_AddDetergent;

extern Todo Todo_SetupLine;

extern Todo Todo_TransferSanitizerToLine ;
extern Todo Todo_ReportRinseOK;
extern Todo Todo_RedoRinse;
extern Todo Todo_ManualFMTClean;

extern Todo Todo_PositionKeg;
extern Todo Todo_NoMoreKegs;

extern Todo Todo_InoculateInKettle;

void setupLine(byte config);
bool setupLineIsReady(bool needConfirmation = false);


#endif

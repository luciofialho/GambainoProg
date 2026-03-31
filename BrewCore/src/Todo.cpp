#include <Todo.h>
#include <Level.h>
#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include <Status.h>
#include <Variables.h>
#include <UI.h>

const char EMPTYSTRING[1] = {'\0'};

const char *lineConfigName[NUMLINECONFIGS] = {
  "Pre Brew",
  "Infusion",
  "Brew",  
  "Brew Top-up",
  "Rinse",
  "Detergent",
  "FMT CIP",
  "Calibration"
  };

byte _numTodos;

Todo* Todos[MAXTODOS];

Todo Todo_StartBrew               ("Prepare for brewing start");
Todo Todo_Sanitizer               ("Add sanitizer to FMT (for ");
Todo Todo_CleanBK                 ("Clean BK");
Todo Todo_MashRest                ("Report when ready for circulation");
Todo Todo_RunOffComplete          ("Report SPARGE completion");
Todo Todo_FirstWortHopping        ("Add first wort hop");
Todo Todo_PrepareTopUpPot         ("Prepare top-up water pot");
Todo Todo_Addition1               ("Addition #1");
Todo Todo_Addition2               ("Addition #2");
Todo Todo_Addition3               ("Addition #3");
Todo Todo_Addition4               ("Addition #4");
Todo Todo_Addition5               ("Addition #5");
Todo Todo_WhirlpoolHopAddition    ("Whirlpool hop Addition");
Todo Todo_AllowTransferStart      ("Prepare for transfer start");
Todo Todo_TurnTransferPumpOn      ("Report when to turn pump on");
Todo Todo_SwitchBKValves          ("Report when to switch extraction valves");
Todo Todo_ReportTransferEnd       ("Report when transfer is complete");
Todo Todo_FinishTopUpWater        ("Report Top-up water transfer completion");
Todo Todo_AddDetergent            ("Add detergent (for ");

Todo Todo_SetupLine               ("Setup line to ");
Todo Todo_TransferSanitizerToLine ("Push sanitizer from FMT to line");
Todo Todo_PurgeLine               ("Purge line and collect sanitizer");

Todo Todo_ReportRinseOK           ("Report rinse complete");
Todo Todo_RedoRinse               ("Repeat rinse");
Todo Todo_ManualFMTClean          ("Manually clean and setup for cleaning/rinse");

Todo Todo_PositionKeg             ("Position keg for cleaning/rinse");
Todo Todo_NoMoreKegs              ("Report no more kegs");


Todo Todo_InoculateInKettle       ("Inoculate in wort");  

bool lineAlreadyConfigured = false;

Todo::Todo(const char *Action)
{
    secondsStart = 0;
    secondsDismiss = 0;
    isOverdue = 0;

    if (_numTodos < MAXTODOS) {
        Todos[_numTodos] = this;
        idx = _numTodos;
        _numTodos++;
        
      strncpy(actionLabel,Action,ACTIONLABELLENGTH);

      secondsDismiss = 0;
      isOverdue = false;
      secondsStart = 0;
    }
    else {
      alert("FIRMWARE ERROR: Exceeded maximum number of to do items");
      while(1) delay(1000);
    }
    suffixStr_ = EMPTYSTRING;
}

void Todo::start(const char *suffixStr,int secondsFromNow)
{
  if (secondsFromNow==0)
    secondsStart = 1;
  else
    secondsStart = secondsFromNow + millis() / 1000;
  secondsDismiss = 0;
  isOverdue = false;
  say("Todo started: %s %d %ld",actionLabel,secondsFromNow, (secondsStart));

  if (suffixStr)
    suffixStr_ = suffixStr;
  else
    suffixStr_ = EMPTYSTRING;
}

void Todo::dismiss() {
  isOverdue = false;
  secondsDismiss = millis()/1000;
  for (byte i = 0; i<3; i++)
    if (autoDryTodo[i] == this) {
      //autoDryTodo[i] = NULL;
      //setZeroVolume(i);
    }
      
 
  say("Todo dismissed: %s",actionLabel);
}

void Todo::reset()
{
  secondsDismiss = 0;
  isOverdue = false;
  secondsStart = 0;
  for (byte i = 0; i<3; i++)
    if (autoDryTodo[i] == this) {
      //setZeroVolume(i);
      autoDryTodo[i] = NULL;
    }
}

bool Todo::notStarted()
{
    return (secondsStart == 0);
}

bool Todo::active()
{
    return (secondsStart != 0) && (secondsDismiss == 0);
}

bool Todo::TodoIsReady() {
  bool res;
  if (secondsDismiss)
    res = true;
  else
    res = false;
  return res;
}

bool Todo::neededTodoIsReady()
{
    if (secondsDismiss)
        return true;
    else {
        if (!isOverdue) {
            if (!active()) 
              start();          
            isOverdue = true;
            sound_ToDo();
        }
        return false;
    }
}

char Todo::status()
{
  int now = millis()/1000;
  
  if (secondsDismiss)
    return 'D';
  else 
    if (isOverdue)
      return 'O';
    else
      if (active()) 
        if (secondsStart>1) 
          if (secondsStart > now)
            if (secondsStart - now < 1*MINUTES)
              return 'S';
            else
              return 'W';
          else
            return 'O';
        else
          return 'A';
      else
        return 'I';
}

Todo * Todo::getByIndex(byte idx)
{
    return Todos[idx];
}

char * Todo::getJSON(char *buf)
{
    char Status = status();
    char label[80];
    int Cd = secondsStart - millis()/1000;
    if (Status == 'S' || Status == 'W' || Status == 'O' || Status == 'A') {
        switch (Status) {
            case 'S':
                sprintf(label,"Standby: %s%s [%02d:%02d]",actionLabel,suffixStr_,int(Cd / 60), Cd-int(Cd/60)*60);
                break;
            case 'W':
                sprintf(label,"%s%s [%02d:%02d]", actionLabel,suffixStr_,int(Cd / 60), Cd-int(Cd/60)*60);
                break;
            case 'O':
                sprintf(label,"%s%s", actionLabel,suffixStr_);
                break;
            default:
                sprintf(label,"%s%s", actionLabel,suffixStr_);
                break;
        }
        sprintf(buf,"\"%d\":{\"ID\":\"%02d\",\"Name\":\"%s\",\"Status\":\"%c\"}", idx, idx, label, Status);
    }
    else
        buf[0] = 0;
    return buf;
}

int Todo::numTodos()
{
    return _numTodos;
}

void Todo::resetTodos()
{
    for (int i = 0; i < _numTodos; i++)
        Todo::getByIndex(i) -> reset();
}

void Todo::monitorTodos()
{
    Todo *t;
    Todo *todo = NULL;
    byte alertLevel = 0;
    
    for (int i = 0; i < Todo::numTodos(); i++) {
        t = Todo::getByIndex(i);
        if (t->status() == 'S')
            if (alertLevel == 0)
                alertLevel = 1;
                
        if (t->status() == 'A' || t->status() == 'O') {
            if (!todo)
                todo = t;
            if (t->status() == 'A' && alertLevel < 2)
                alertLevel = 2;
            if (t->status() == 'O')
                alertLevel = 3;
        }
    }
        
/*    if (todo)
        if (NextButton())
            todo->dismiss();
            */
        
    if (alertLevel) {
        ; /**** Implementar beeps e pisca pisca ****/
    }
}

void setupLine(byte config) {
  if (config != LineConfiguration.getProgValue()) {
    Todo_SetupLine.start(lineConfigName[config-1]);
    LineConfiguration = config;
    lineAlreadyConfigured = false;
  }
  else {
    if (Todo_SetupLine.notStarted()) {
      lineAlreadyConfigured = true;
      say("Line already configured to %s - todo dismissed",lineConfigName[config-1]);
    }
  }
}

bool setupLineIsReady(bool needConfirmation) {
  return (!needConfirmation && lineAlreadyConfigured) || Todo_SetupLine.neededTodoIsReady();
}

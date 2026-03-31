#include "UI.h"
#include "ProcVar.h"
//#include <WiFi.h>
#include "GambainoCommon.h"
#include "EspNowPackets.h"
#include <BrewCoreCommon.h>
#include <Status.h>
#include <IPC.h>
#include <Variables.h>
#include <Todo.h>
#include <WaterHeatControl.h>
#include "IOTK.h"
#include "IOTK_NTP.h"
#include "IOTK_ESPAsyncServer.h"
#include <SPIFFS.h>
#include <ff.h>
#include "EasyBuzzer.h"
#include "monitor.h"
#include "IOTK_SimpleMail.h"
#include "CloudLog.h"
#include "level.h"
#include "esp_task_wdt.h"



char alertMessage[200];
char consoleMessage[300];
char consoleMessageNext[300];

extern const int BIGBUFFERSIZE;
extern char bigBuffer[];

#define MONITORINTERVAL    500

#define INSTRNOP                -99
#define INSTRHELP               0
#define INSTRSTARTPROG          1
#define INSTRFORCE              2
#define INSTRRELEASE            3
#define INSTRRELEASEALL         4
#define INSTRSHOWMATH           5
#define INSTRLIST               6
#define INSTRGETJSON            7
#define INSTRSTATUSLIST         8
#define INSTRIPCSTATUSLIST      9
#define INSTRECHO               10
#define INSTRMONITOR            11
#define INSTRSTOPMONITOR        12
#define INSTRSCANDALLAS         13
#define INSTRRESET              14
#define INSTRREINIT             15
#define INSTRLISTTODO           16
#define INSTRGETTODOJSON        17
#define INSTRDISMISSTODO        18
#define INSTRSAVEPROCESS        19
#define INSTRRESUMEPROCESS      20
#define INSTRFACTORYRESET       21
#define INSTRCOLDSTATUS         22
#define INSTRSTARTDIAG          23
#define NUMCOMMANDS             24

#define INSTRSYNTAX_CMDONLY     0
#define INSTRSYNTAX_TAG         1
#define INSTRSYNTAX_TAGVALUE    2
#define INSTRSYNTAX_STRING      3
#define INSTRSYNTAX_VALUE       4

const int INSTRSYNTAX[NUMCOMMANDS] = {
  INSTRSYNTAX_CMDONLY,        // INSTRHELP
  INSTRSYNTAX_VALUE,          // INSTRSTARTPROG
  INSTRSYNTAX_TAGVALUE,       // INSTRFORCE
  INSTRSYNTAX_TAG,            // INSTRRELEASE
  INSTRSYNTAX_CMDONLY,        // INSTRRELEASEALL
  INSTRSYNTAX_CMDONLY,        // INSTRSHOWMATH
  INSTRSYNTAX_STRING,         // INSTRLIST
  INSTRSYNTAX_VALUE,          // INSTRGETJSON
  INSTRSYNTAX_CMDONLY,        // INSTRSTATUSLIST
  INSTRSYNTAX_CMDONLY,        // INSTRIPCSTATUSLIST
  INSTRSYNTAX_STRING,         // INSTRECHO
  INSTRSYNTAX_TAG,            // INSTRMONITOR
  INSTRSYNTAX_CMDONLY,        // INSTRSTOPMONITOR
  INSTRSYNTAX_CMDONLY,        // INSTRSCANDALLAS
  INSTRSYNTAX_CMDONLY,        // INSTRRESET
  INSTRSYNTAX_CMDONLY,        // INSTRREINIT
  INSTRSYNTAX_CMDONLY,        // INSTRLISTTODO
  INSTRSYNTAX_CMDONLY,        // INSTRGETTODOJSON
  INSTRSYNTAX_VALUE,          // INSTRDISMISSTODO
  INSTRSYNTAX_CMDONLY,        // INSTRSAVEPROCESS
  INSTRSYNTAX_CMDONLY,        // INSTRRESUMEPROCESS
  INSTRSYNTAX_CMDONLY,        // INSTRFACTORYRESET
  INSTRSYNTAX_CMDONLY,        // INSTRCOLDSTATUS
  INSTRSYNTAX_VALUE           // STARTDIAG
};
bool requestFromIP = false;
int echoMode = 0;
ProcVar * monitoredVar;

void getDataLog(AsyncWebServerRequest *request);
void ColdSideWebPage(AsyncWebServerRequest *request);
void commandHelp(AsyncWebServerRequest *request);
void calibrResultPage(AsyncWebServerRequest *request);

const int c = 261;
const int d = 294;
const int e = 329;
const int f = 349;
const int g = 391;
const int gS = 415;
const int a = 440;
const int aS = 455;
const int b = 466;
const int cH = 523;
const int cSH = 554;
const int dH = 587;
const int dSH = 622;
const int eH = 659;
const int fH = 698;
const int fSH = 740;
const int gH = 784;
const int gSH = 830;
const int aH = 880;

void ethernetProcessString_and_Page(AsyncWebServerRequest *request, const char *st, const char *pageName) {
  File f = SPIFFS.open(pageName, "r");
  if (!f) { request->send(404, "text/plain", "File not found"); return; }

  size_t stLen = st ? strlen(st) : 0;
  auto stBuf = std::shared_ptr<char>(stLen ? new char[stLen + 1] : nullptr, std::default_delete<char[]>());

  if (stLen > 0) {
    memcpy(stBuf.get(), st, stLen);
    stBuf.get()[stLen] = '\0';
  }

  AsyncWebServerResponse *response =
    request->beginChunkedResponse("text/html; charset=utf-8",
      [f, stBuf, stLen](uint8_t *buffer, size_t maxLen, size_t index) mutable -> size_t {

        if (!buffer || maxLen == 0) return 0;

        if (index < stLen) {
          size_t n = ((stLen - index) < maxLen) ? (stLen - index) : maxLen;
          memcpy(buffer, stBuf.get() + index, n);
          return n;
        }

        if (!f.seek(index - stLen)) {
          f.close();
          return 0;
        }

        size_t n = f.read(buffer, maxLen);
        if (n == 0) f.close();
        return n;
      });

  request->send(response);
}


inline bool endsWith(const char *str, const char *suffix) {
  size_t strLen = strlen(str);
  size_t suffixLen = strlen(suffix);
  if (suffixLen > strLen) return false;
  return strcmp(str + strLen - suffixLen, suffix) == 0;
}

bool ethernetProcessPage(AsyncWebServerRequest *request, const char *pageName) {
  if (!SPIFFS.exists(pageName)) {
    Serial.printf("File not found: %s\n", pageName);
    request->send(404, "text/plain", "File not found");
    return false;
  }

  AsyncClient *client = request->client();
  if (!client) {
    Serial.println("No client connection");
    request->send(500, "text/plain", "Internal error");
    return false;
  }

  const char *contentType;
  const char *cacheControl;

  if (endsWith(pageName, ".html")) {
    contentType = "text/html;charset=utf-8";
    cacheControl = "no-cache, no-store, must-revalidate";
  }
  else {
    contentType = nullptr;
    if (endsWith(pageName, ".css")) contentType = "text/css";
    else if (endsWith(pageName, ".js")) contentType = "application/javascript";
    else if (endsWith(pageName, ".png")) contentType = "image/png";
    else if (endsWith(pageName, ".jpg") || endsWith(pageName, ".jpeg")) contentType = "image/jpeg";
    else if (endsWith(pageName, ".gif")) contentType = "image/gif";
    else if (endsWith(pageName, ".ico")) contentType = "image/x-icon";
    else if (endsWith(pageName, ".svg")) contentType = "image/svg+xml";
    else if (endsWith(pageName, ".json")) contentType = "application/json";
    else if (endsWith(pageName, ".woff") || endsWith(pageName, ".woff2")) contentType = "font/woff2";

    if (contentType) {
      cacheControl = "max-age=604800";  // 1 semana
    }
    else {
      contentType = "text/plain";
      cacheControl = "no-cache";
    }
  }

  AsyncWebServerResponse *response = request->beginResponse(SPIFFS, pageName, contentType);
  response->addHeader("Cache-Control", cacheControl);
  request->send(response);
  return true;
}



void SIPOutput(AsyncWebServerRequest *request,const char *s, int beep) {

  if (!requestFromIP || echoMode || beep)
    Serial.println(s);
  
  if (requestFromIP && request) {
    request->send(200,"text/html", s);
  }

}

void SIPOutput(AsyncWebServerRequest *request,char *s, int beep) {
    SIPOutput(request, (const char *) s, beep);
}

void gambaino_JS(AsyncWebServerRequest *request) {
  char st[512];
  uint32_t ip;
  byte *oct;

  ip = WiFi.localIP();
  oct = (byte*)&ip;
  snprintf(st, 511,  "var URLArduino = \"http://%hhu.%hhu.%hhu.%hhu/\";\nSTMINMLT_ = %d; STMAXMLT_ = %d; STMINBK1_ = %d; STMAXBK1_ = %d; STMINBK2_ = %d; STMAXBK2_ = %d; STTRAN_ = %d;\n",
      oct[0], oct[1], oct[2], oct[3], MASHGRAINREST, PREPSPARGE, SPARGE, KS_WHIRLPOOL, WAITBOIL, WHIRLPOOLREST, TRANSFER);
  ethernetProcessString_and_Page(request, st,"/www/gambaino.js");
}


char cmdSerialBuf[MAXLINE];



int serialPooling(char *cmdBuf)  {
  int result = 0;
  char a;
  static unsigned long lastMonitorOutput;

  if (monitoredVar && MILLISDIFF(lastMonitorOutput, MONITORINTERVAL)) {
      char buf[80];
      Serial.print("   ");
      Serial.print(monitoredVar->displayString(buf));
      if (monitoredVar->tag()==BKLevel.tag()) {
        Serial.print("  pin input: ");
        Serial.print(analogReadStable(PINBKLEVEL));
      }
      else if (monitoredVar->tag()==MLTTopLevel.tag()) {
        Serial.print("  pin input: ");
        Serial.print(analogReadStable(PINMLTTOPLEVEL));
      }
      else if (monitoredVar->tag()==KegLiquidSensor.tag()) {
        Serial.print("  pin input: ");
        Serial.print(analogRead(PINKEGLIQUIDSENSORS));
      } 

      Serial.println();
      lastMonitorOutput = millis();
  }
  
  if (Serial.available()>0) {
    a = Serial.read();
    if ((a == ';') || (a == 10) || (a == 13)) {
      if (cmdSerialBuf[0]) {
        strcpy(cmdBuf, cmdSerialBuf);
        cmdSerialBuf[0] = '\0';
        requestFromIP = false;
        result = 1;    // new command to process
      }
    }
    else {
      int p = strlen(cmdSerialBuf);
      if (p<MAXLINE-2) {
        cmdSerialBuf[p+1] = '\0';
        cmdSerialBuf[p]   = a;
      }
    }
  }
  
  return result;
}


void ethernetCmdReceived(AsyncWebServerRequest *request) {
  char lineBuf[161];
  if (   request->url().substring(0,5) == "/www/" || request->url()=="/") 
    if (request->url()=="/www/gambaino.js") {
      gambaino_JS(request);
      checkPerfSentinel("gambaino.JS");
    }
    else {
      if (request->url()=="/") {
        request->redirect("/www/index.html");
        checkPerfSentinel("redirect");
      }
      else {
        if (!ethernetProcessPage(request,request->url().c_str()))
          serverHandleNotFound(request);
        checkPerfSentinel(request->url().c_str());
      }
    }
  else {
    strncpy(lineBuf,request->url().c_str()+1,120);
    requestFromIP = true;
    cmdProcess(request, lineBuf);
    checkPerfSentinel(request->url().c_str());
  }
}

boolean lastHas = false;

void JSONOutput(AsyncWebServerRequest *request, byte token2Value=1) {
    char lineBuf[120];

    strcpy(bigBuffer,"{");//    server.sendContent("{");


    for (int i = 0; i < ProcVar::numVars(); i++) {
        ProcVar *x;
        x = (ProcVar::ProcVarByIndex(i));
        if (x->JSONGroup() & token2Value) 
          strnncat(bigBuffer,x->JSONString(lineBuf),BIGBUFFERSIZE); 
    }
    
    sprintf(lineBuf,"\"StatusName\":\"%s\",", statusNames[(int)Status]);
    strnncat(bigBuffer,lineBuf,BIGBUFFERSIZE); 
    

    sprintf(lineBuf,"\"SubStatusName\":\"%s\",", (SubStatusLabel && SubStatusLabel[0]) ? SubStatusLabel : "---");
    strnncat(bigBuffer,lineBuf,BIGBUFFERSIZE); 

    
    sprintf(lineBuf,"\"IPCStatusName\":\"%s\",", IPCStatusNames[(int)IPCStatus]);
    strnncat(bigBuffer,lineBuf,BIGBUFFERSIZE);

    sprintf(lineBuf,"\"CirculationModeName\":\"%s\",", CircStatusNames[int(CirculationMode)]);
    strnncat(bigBuffer,lineBuf,BIGBUFFERSIZE);
    
    char buf[400],buf2[300];
    sprintf(buf,"\"ConsoleMessage\":\"%s\",",getConsoleMessage(buf2)); strnncat(bigBuffer,buf,BIGBUFFERSIZE);
    sprintf(buf,"\"AlertMessage\":\"%s\",",getAlertMessage());strnncat(bigBuffer,buf,BIGBUFFERSIZE);
    
    
    sprintf(lineBuf,"\"cycles\":\"%li\",\n\"avrCycleTime\":\"%li\",\n\"maxCycleTime\":\"%li\",",numCycles, averageCycle,longestCycle);
    strnncat(bigBuffer,lineBuf,BIGBUFFERSIZE); 
    
    sprintf(lineBuf,"\"millis\":\"%li\",\n\"freemem\":\"%i\",\n\"TimeInStatus\":\"%li\",\n\"IPCTimeInStatus\":\"%li\",\"Todo\":{ ",millis(),(int)0/***xxxesp32*freeMemory()*/,long(float(TimeInStatus)),long(float(IPCTimeInStatus)));
    strnncat(bigBuffer,lineBuf,BIGBUFFERSIZE); 

    bool first = true;
    for (int i = 0; i < Todo::numTodos(); i++) {
        Todo::getByIndex(i)->getJSON(lineBuf);
        if (lineBuf[0]) {
            if (first)
                first = false;
            else
                strnncat(bigBuffer,",",BIGBUFFERSIZE);
            strnncat(bigBuffer,lineBuf,BIGBUFFERSIZE);
        }
    }
    strnncat(bigBuffer,"}}",BIGBUFFERSIZE);

    SIPOutput(request,bigBuffer);

}


void getJson(AsyncWebServerRequest *request) {
    JSONOutput(request,request->argName(1).toInt());
}


void cmdProcess(AsyncWebServerRequest *request,char *cmd) {
    int part=0;
    int i = 0;
    int inToken = 0;
    byte stx;
    char cmdToken[MAXTOKENS][MAXLINE];
    float token2Value = 0;
    int  instr = 0;
    int  err=0;
    bool compositeIPCommand = false;
    
    for (int i = 0; i<3; i++)
        cmdToken[i][0] = '\0';

    while (cmd[strlen(cmd)-1] == ' ')
        cmd[strlen(cmd)-1] = 0;
    
    i=0;        
    while (char a = cmd[i]) {
        if (a==' ' || a=='_') {
            if (inToken) {
                part++;
                if (part >= MAXTOKENS) {
                    err = 1; // too many tokens
                    goto process;
                }
                inToken = 0;
            }
        }
        else {
            inToken = 1;
            if (a >= 'A' && a <= 'Z')
                a += 32;
            int p = strlen(cmdToken[part]);
            if (p<MAXLINE-2) {
                cmdToken[part][p+1] = '\0';
                cmdToken[part][p]   = a;
            }
        }
                
        i++;
    }


    if (cmdToken[0][0]=='.') {
        compositeIPCommand = true;
        for (i=1;cmdToken[0][i-1];i++)
            cmdToken[0][i-1] = cmdToken[0][i];
    }
    else   
        compositeIPCommand = false;
    
    instr = -1;
    if (!strcmp(cmdToken[0],"favicon.ico"))                                     instr = INSTRNOP;
    if (!strcmp(cmdToken[0],"help")          || !strcmp(cmdToken[0],"?"))       instr = INSTRHELP;
    if (!strcmp(cmdToken[0],"startprogram")  || !strcmp(cmdToken[0],"sp"))      instr = INSTRSTARTPROG;
    if (!strcmp(cmdToken[0],"force")         || !strcmp(cmdToken[0],"fo"))      instr = INSTRFORCE;
    if (!strcmp(cmdToken[0],"release")       || !strcmp(cmdToken[0],"re"))      instr = INSTRRELEASE;
    if (!strcmp(cmdToken[0],"releaseall")    || !strcmp(cmdToken[0],"ra"))      instr = INSTRRELEASEALL;
    if (!strcmp(cmdToken[0],"showmath")      || !strcmp(cmdToken[0],"sm"))      instr = INSTRSHOWMATH;
    if (!strcmp(cmdToken[0],"list")          || !strcmp(cmdToken[0],"li"))      instr = INSTRLIST;
    if (!strcmp(cmdToken[0],"getjson")       || !strcmp(cmdToken[0],"gj"))      instr = INSTRGETJSON;
    if (!strcmp(cmdToken[0],"statuslist")    || !strcmp(cmdToken[0],"sl"))      instr = INSTRSTATUSLIST;
    if (!strcmp(cmdToken[0],"ipcstatuslist") || !strcmp(cmdToken[0],"isl"))     instr = INSTRIPCSTATUSLIST;
    if (!strcmp(cmdToken[0],"echo")          || !strcmp(cmdToken[0],"ec"))      instr = INSTRECHO;
    if (!strcmp(cmdToken[0],"monitor")       || !strcmp(cmdToken[0],"mo"))      instr = INSTRMONITOR;
    if (!strcmp(cmdToken[0],"stopmonitor")   || !strcmp(cmdToken[0],"sm"))      instr = INSTRSTOPMONITOR;
    if (!strcmp(cmdToken[0],"scandallas")    || !strcmp(cmdToken[0],"sd"))      instr = INSTRSCANDALLAS;
    if (!strcmp(cmdToken[0],"reset"))                                           instr = INSTRRESET;    
    if (!strcmp(cmdToken[0],"reinit"))                                          instr = INSTRREINIT;    
    if (!strcmp(cmdToken[0],"listtodo")      || !strcmp(cmdToken[0],"lt"))      instr = INSTRLISTTODO;
    if (!strcmp(cmdToken[0],"gettodogjson")  || !strcmp(cmdToken[0],"tj"))      instr = INSTRGETTODOJSON;
    if (!strcmp(cmdToken[0],"dismisstodo")   || !strcmp(cmdToken[0],"dt"))      instr = INSTRDISMISSTODO;
    if (!strcmp(cmdToken[0],"saveprocess")   || !strcmp(cmdToken[0],"save"))    instr = INSTRSAVEPROCESS;
    if (!strcmp(cmdToken[0],"resumeprocess") || !strcmp(cmdToken[0],"resume"))  instr = INSTRRESUMEPROCESS;
    if (!strcmp(cmdToken[0],"factoryreset"))                                    instr = INSTRFACTORYRESET;
    if (!strcmp(cmdToken[0],"coldstatus")    || !strcmp(cmdToken[0],"cold"))    instr = INSTRCOLDSTATUS;
    if (!strcmp(cmdToken[0],"startdiag"))                                       instr = INSTRSTARTDIAG;


    if (instr==-1) {
        err=4;
        goto process;
    }

    stx = INSTRSYNTAX[instr];
    
    if ((stx == INSTRSYNTAX_CMDONLY && cmdToken[1][0])
      ||((stx == INSTRSYNTAX_TAG || stx==INSTRSYNTAX_STRING || stx == INSTRSYNTAX_VALUE)    && cmdToken[2][0])) {
        err = 1;     // too many tokens
        goto process;
    }
    
    if (((stx == INSTRSYNTAX_TAG || stx == INSTRSYNTAX_STRING || stx == INSTRSYNTAX_VALUE) &&      !cmdToken[1][0])
      ||(stx == INSTRSYNTAX_TAGVALUE && !cmdToken[2][0])) {
        err = 2;     // not enough tokens
        goto process;
    }
      
    {
        int p = 2; if (stx == INSTRSYNTAX_VALUE) p = 1;
        if (cmdToken[p][0]) {
            if (!strcmp(cmdToken[p],"off") || !strcmp(cmdToken[p],"closed") || !strcmp(cmdToken[p],"a"))
                token2Value = 0;
            else if (!strcmp(cmdToken[p],"on") || !strcmp(cmdToken[p],"open") || !strcmp(cmdToken[p],"b"))
                token2Value = 1;
            else {
                if (!(token2Value = atof(cmdToken[p])) && strcmp(cmdToken[p],"0")) {
                    err = 3;   // invalid numeric argument
                    goto process;
                }
            }
        }
    }

    
    process:

    if (instr==INSTRNOP && requestFromIP) {
      request->send(200,"text/plain","");
      return;
    }

    if (!requestFromIP || echoMode || (instr != INSTRGETJSON && instr != INSTRGETTODOJSON && instr != INSTRSTATUSLIST && instr != INSTRIPCSTATUSLIST && instr != INSTRLIST)) 
        if (requestFromIP)
            say("Attending an IP request:   COMMAND: %s",cmd);
        else
            say("Attending a console request:   %ld   COMMAND: %s",millis(),cmd);
    
    if (err) {

        switch (err) {
            case 1:
                SIPOutput(request, "error: Too many arguments",1);
                break;
            case 2:
                SIPOutput(request,"error: Not enough arguments",1);
                break;
            case 3:
                SIPOutput(request,"error: Invalid numeric argument",1);
                break;
            case 4:
            default:
                SIPOutput(request,"error: Invalid command",1);               
                //serverHandleNotFound(request);
                break;
        }
        
    }
    else {
        switch (instr) {
            case INSTRHELP:
                commandHelp(request);
                break;
                
            case INSTRSTARTPROG:
                if (token2Value<=NUMPROGRAMS || (token2Value>100 && token2Value/100<=NUMPROGRAMS)) {
                  programToStart = token2Value;
                  SIPOutput(request,"Program will start");
                }
                else {
                  say("Invalid program number");
                  SIPOutput(request,"ERROR: Invalid program number");
                }
                break;

            case INSTRFORCE:
            case INSTRRELEASE:
                {
                    ProcVar *x;
                    x = ProcVar::ProcVarByID(cmdToken[1]);
                    if (!x)
                        SIPOutput(request,"ERROR: Invalid process variable");
                    else {
                        char Line[MAXLINE*3]; 
                        
                        if (instr == INSTRFORCE) {
                            char fbuf [20];
                            x->override(token2Value);
                            sprintf(Line,"Var %s overrided to %s",x->tag(),dtostrf(token2Value,3,1,fbuf));
                        }
                        else {
                            x->override();
                            sprintf(Line,"Var %s restored to processed value",x->tag());
                        }
                        if (x->persistenceGroup() == CONFIGPERSISTENCE)
                            ProcVar::writeToEEPROM(CONFIGPERSISTENCE);
                        if (compositeIPCommand) 
                            JSONOutput(request);
                        else
                            SIPOutput(request,Line,0);
                    }
                }
                break;

            case INSTRRELEASEALL:
              ProcVar::releaseAllOverrides();
              SIPOutput(request,"All overrided variables restored to processed values");
              break;
              
            case INSTRSHOWMATH:
              forceWebOutput = true;
              calculateVolumesAndTemperatures(true);
              forceWebOutput = false;
              break;
            
            case INSTRLIST:
                {
                  bigBuffer[0]=0;

                  char lineBuf[120];
                  for (int i = 0; i < ProcVar::numVars(); i++) {
                    ProcVar *x;
                    x = (ProcVar::ProcVarByIndex(i));
                    if (  (cmdToken[1][0]=='a') || 
                          (cmdToken[1][0]==x->group()+32) || 
                          (cmdToken[1][0]=='o' && x->isOverrided()) ||
                          (cmdToken[1][0]=='d' && x->modeIsDallas())) {
                      strnncat(bigBuffer,x->displayString(lineBuf),BIGBUFFERSIZE);
                      if (requestFromIP)
                        strnncat(bigBuffer,"<br>",BIGBUFFERSIZE);
                      else
                        strnncat(bigBuffer,"\n\r",BIGBUFFERSIZE);
                    }
                  }       
                  SIPOutput(request,bigBuffer);
                  break;
                }
                    
            case INSTRGETJSON:
              JSONOutput(request, token2Value);
              break;
            
            case INSTRSTATUSLIST: 
              {
                char line[80];
                char buf [STATUSNAMESIZE];
                strcpy(bigBuffer,"{");
                for (int i = 0; i<NUMSTATUS; i++)
                  if (!skipStatus(i)) {
                    strcpy(buf, statusNames[i]);
                    sprintf(line,"\"%i\":\"%s\"%c",
                            i, buf, (i<NUMSTATUS - 1)?',':'}');
                    strnncat(bigBuffer,line,BIGBUFFERSIZE);
                  }
                request->send(200,"text/html",bigBuffer);
              }
                break;

            case INSTRIPCSTATUSLIST:
              {
                char line[80];
                char buf[IPCSTATUSNAMESIZE];
                strcpy(bigBuffer,"{");
                for (int i = 0; i < NUMIPCSTATUS; i++) {
                  strcpy(buf, IPCStatusNames[i]);
                  sprintf(line,"\"%i\":\"%s\"%c",
                          i, buf, (i < NUMIPCSTATUS - 1) ? ',' : '}');
                  strnncat(bigBuffer,line,BIGBUFFERSIZE);
                }
                request->send(200,"text/html",bigBuffer);
              }
                break;
            
            case INSTRECHO:
                if (!strcmp(cmdToken[1],"on"))  echoMode = 1;
                if (!strcmp(cmdToken[1],"off")) echoMode = 0;
                if (echoMode) 
                    SIPOutput(request,"Echo is on"); 
                else 
                    SIPOutput(request,"Echo is off"); 
                break;

            case INSTRMONITOR:
                if (requestFromIP)
                  SIPOutput(request,"ERROR: MONITOR is for serial console use only",1);
                else
                  monitoredVar = ProcVar::ProcVarByID(cmdToken[1]);
                break;
                
            case INSTRSTOPMONITOR:
                monitoredVar = NULL;
                break;


            case INSTRSCANDALLAS:
                forceWebOutput = true;
                ProcVar::DallasScan(bigBuffer,BIGBUFFERSIZE);
                forceWebOutput = false;
                if (requestFromIP)
                  SIPOutput(request,bigBuffer);
                break;
                
            case INSTRRESET:
                say();
                say("RESET REQUEST RECEIVED");
                say("Gambaino is going bye bye");
                say();
                while (client.connected()) {
                    client.stop();
                    delay(100);
                }
                ESPRestart(request);
                break;

            case INSTRREINIT:
              {
                say();
                say("REINIT REQUEST RECEIVED");
                say();
                int count = 0;
                while (client.connected() && count<100) {
                    client.stop();
                    delay(100);
                    count++;
                }
                dallas1.begin();
                dallas2.begin();
                dallas3.begin();
                server.reset();
                server.end();
                sleep(500);
                server.begin();
                Serial.println("server restarted");
                UI_SETUP();
                say("Reinit done");
              }
              break;

            case INSTRGETTODOJSON:
                {
                    strcpy(bigBuffer,"{");                   
                    char buf[100];
                    bool first = true;
                    for (int i = 0; i < Todo::numTodos(); i++) {
                        Todo::getByIndex(i)->getJSON(buf);
                        if (buf[0]) {
                            if (first)
                                first = false;
                            else
                              strnncat(bigBuffer,",",BIGBUFFERSIZE);
                            strnncat(bigBuffer,buf,BIGBUFFERSIZE);
                        }
                    }
                    strnncat(bigBuffer,"}",BIGBUFFERSIZE);
                    request->send(200,"text/html",bigBuffer);
                }
                break;
                
            case INSTRDISMISSTODO:
                if (atoi(cmdToken[1]) !=0 || !strcmp(cmdToken[1],"0")) 
                    if (atoi(cmdToken[1])==99)
                        clearAlertMessage();
                    else
                        Todo::getByIndex(atoi(cmdToken[1]))->dismiss();
                else
                    alert("FIRMWARE ERROR: invalid argument to TodoDismiss");
                break;
                
            case INSTRSAVEPROCESS: 
              if (Status != STANDBY) {
                ProcVar::writeToEEPROM(PROCESSPERSISTENCE);
                ProcVar::writeToEEPROM(OVERRIDEPERSISTENCE);                
                SIPOutput(request,"Process state saved to EEPROM manually.");
              }
              break;

            case INSTRRESUMEPROCESS: 
              SIPOutput(request,"Process status restored");
              restoringState = true;
              break;             
                
            case INSTRFACTORYRESET:
              SIPOutput(request,"Forced restore to factory settings");
              ProcVar::forceResetToFactory();
              break;
                
            case INSTRCOLDSTATUS:
              if (!requestFromIP)
                SIPOutput(request,"ERROR: COLDSTATUS is for HTTP use only",1);
              else
                ColdSideWebPage(request);
              break;
                
            case INSTRSTARTDIAG:
              //responseConfirmation(request, "Starting diagnostic","./diag");
              //SIPOutput(request, "Starting diagnostic");
              programToStart = PGMDIAGMANUAL;
              diagParameters = token2Value;
              break;

            default:
              serverHandleNotFound(request);
              break;


        }
    }
}

void getDataLog(AsyncWebServerRequest *request)
{
  /* pendencia

  char timestamp[30];
  sprintf(timestamp,"%04d-%02d-%02d %02d_%02d_%02d",
          NTPYear(),NTPMonth(),NTPDay(),NTPHour(),NTPMinute(),NTPSecond());
  S tring header = "attachment; filename=GambainoLog-"+S tring(timestamp)+".csv";
  server.sendHeader("Connection", "close");
  server.sendHeader("Content-Disposition", header);

  char buf[900];
  ProcVar::dataLogRewind();
  ProcVar::dataLogHeader(buf);
  server.sendContent(buf);
  unsigned long int last = millis();
  while (ProcVar::dataLogNextLine(buf)) {
    server.sendContent(S tring(buf));
    if (MILLISDIFF(last,2000)) {
      last = millis();
      yield();
    }
  }
  */
}


void formatColdSideNum(char * buf, float f) {
    if (f == NOTaTEMP || f==85)
      strcpy(buf, "N/A");
    else {
      if (f>0)
        if (f<100)
            dtostrf(f,2,1,buf);
        else
            dtostrf(f,4,0,buf);
      else if (f >= -0.05 && f <= 0.05)
        strcpy(buf, " 0.0");
      else if (f >= -9.5) {
        dtostrf(-f,1,1,buf+1);
        buf[0] = '-';
      }
      else {
        dtostrf(-f,2,0,buf+1);
        buf[0] = '-';
      }
    }
}



void ColdSideWebPage(AsyncWebServerRequest *request) {
    char stOff[]="off";
    char stOn[]="ON";
    char stHeater[] = "HEATER";
    char stChill[] = "ON";

    char st[1024];

    char f1at[10],f1t[10],f1st[10],f2at[10],f2t[10],f2st[10],cat[10],ct[10],cst[10],cbat[10],cbt[10],cbst[10],env[10],f1lt[10],f2lt[10],
         uptime[20],time[20];
/*
    formatColdSideNum(f1at,Fermenter1Temp);
    formatColdSideNum(f1t,Fermenter1TargetTemp);
    formatColdSideNum(f2at,Fermenter2Temp);
    formatColdSideNum(f2t,Fermenter2TargetTemp);
    formatColdSideNum(cat,CellarTemp);
    formatColdSideNum(ct,CellarTargetTemp);
    formatColdSideNum(cbat,ColdBankTemp);
    formatColdSideNum(cbt,ColdBankTargetTemp);
    formatColdSideNum(env,EnvironmentTemp);
    formatColdSideNum(f1lt,Fermenter1SlowTargetTemp);
    formatColdSideNum(f2lt,Fermenter2SlowTargetTemp);
    formatedUptime(uptime);
    NTPFormatedDateTime(time);

    snprintf(st,511,"<script>\
function loadValues() {\
f1at.innerHTML='%s';\n\
f1t.value='%s';\n\
f1st.innerHTML='%s';\n\
f2at.innerHTML='%s';\n\
f2t.value='%s';\n\
f2st.innerHTML='%s';\n\
cat.innerHTML='%s';\n\
ct.value='%s';\n\
cst.innerHTML='%s';\n\
cbat.innerHTML='%s';\n\
cbt.value='%s';\n\
cbst.innerHTML='%s';\n\
env.innerHTML='%s';\n\
f1lt.value='%s';\n\
f2lt.value='%s';\n\
uptime.innerHTML='%s';\n\
time.innerHTML='%s';\n\
}\n\
</script>",
    f1at,f1t,Fermenter1Control.asBoolean() ? stChill : Fermenter1Heater.asBoolean() ? stHeater : stOff,
    f2at,f2t,Fermenter2Control.asBoolean() ? stChill : Fermenter2Heater.asBoolean() ? stHeater : stOff,
    cat,ct,CellarControl.asBoolean() ? stOn : stOff,
    cbat,cbt,ColdBankControl.asBoolean() ? stOn : stOff,
    env,
    f1lt,f2lt,
    uptime,time);
*/
  ethernetProcessString_and_Page(request, st,"/www/coldSide.html");

}

float convertColdSideValue(const char *st) {
  float f= atof(st);
  if (f==0) {
    bool trueZero = true;
    for (int i=0;st[i];i++)
      if (st[i] != '.' && (st[i]<'0' || st[i]>'9'))
        trueZero = false;
    if (!trueZero)
      f = NOTaTEMP;
  }
  return f;
}

void saveColdSide(AsyncWebServerRequest *request) {
  for (int i = 0; i<request->args(); i++) {
    ProcVar *var = ProcVar::ProcVarByID(request->argName(i).c_str());
    if (var) {
      char buf[10];
      if (convertColdSideValue(request->arg(i).c_str()) != float(*var)) {
        Serial.print(var->alias()); Serial.print(" = "); Serial.println(request->arg(i));      
        (*var) = convertColdSideValue(request->arg(i).c_str());
      }

    }
  }

  ProcVar::writeToEEPROM(CONFIGPERSISTENCE);
  responseConfirmation(request,"Cold side set points saved","./cold");
}

void formatRecipeNum(char *buf, float f,int decPlaces=1, const char * strIfMissing=NULL, int missingValue=0) {
  if (f == missingValue && strIfMissing)
    strcpy(buf, strIfMissing);
  else {
    char fmt[10];
    sprintf(fmt,"%%.%if",decPlaces);
    sprintf(buf,fmt,f);
  }
}

#define VARTYPESIMPLE 0
#define VARTYPENA 1
#define VARTYPEADD 2
#define RECIPEDATAMAXSIZE 4096

void addRcpVar(char *buf, ProcVar &var, int decPlaces, byte type) {
  char fbuf1[10],fbuf2[10];
  switch(type) {
    case VARTYPESIMPLE:
      formatRecipeNum(fbuf1,(float)(var),decPlaces);
      break;
    case VARTYPENA:
      formatRecipeNum(fbuf1,(float)(var),decPlaces, "N/A");
      break;
    case VARTYPEADD:
      formatRecipeNum(fbuf1,(float)(var),decPlaces, "N/A",-1);
      break;
  }
  formatRecipeNum(fbuf2,(float)(var),2);

  char lineBuf[256]; 
  snprintf(lineBuf,255,"setupVar(%s,'%s','%s');\n",  var.tag(),fbuf1,fbuf2);
  strnncat(buf,lineBuf,RECIPEDATAMAXSIZE);
}

void recipeWebPage(AsyncWebServerRequest *request) {
  char st[RECIPEDATAMAXSIZE+1];
  strcpy(st,"<script>\n  function loadValues() {\n\
function setupVar(element,defValue,missValue)\
{\n\
with (element) {\n\
value = defValue;\n\
addEventListener('change', function() {\n\
className = getClassName(value, missValue);\n\
});\n\
}\n\
}\n");   
    
    

  addRcpVar(st,RcpBatchNum,0,VARTYPESIMPLE);
  addRcpVar(st,RcpStartTime,1,VARTYPESIMPLE); 
  addRcpVar(st,RcpFermenterCleaning,0,VARTYPESIMPLE);
  addRcpVar(st,RcpTargetFermenter,0,VARTYPESIMPLE);
  addRcpVar(st,RcpTopupWater,0,VARTYPESIMPLE);
  addRcpVar(st,RcpGrainWeight,1,VARTYPESIMPLE);
  addRcpVar(st,RcpMashTime,0,VARTYPESIMPLE);
  addRcpVar(st,RcpMashTemp,1,VARTYPESIMPLE);
  addRcpVar(st,RcpWaterGrainRatio,2,VARTYPESIMPLE);
  addRcpVar(st,RcpRampGrainWeight,1,VARTYPENA);
  addRcpVar(st,RcpRampTime,0,VARTYPENA);
  addRcpVar(st,RcpRampTemp,1,VARTYPESIMPLE);
  addRcpVar(st,RcpCaramelizationBoil,0,VARTYPENA);
  addRcpVar(st,RcpBoilTime,0,VARTYPESIMPLE);
  addRcpVar(st,RcpBoilTemp,1,VARTYPENA);
  addRcpVar(st,RcpFirstWortHopping,0,VARTYPESIMPLE);
  addRcpVar(st,RcpAddition1,0,VARTYPEADD);
  addRcpVar(st,RcpAddition2,0,VARTYPEADD);
  addRcpVar(st,RcpAddition3,0,VARTYPEADD);
  addRcpVar(st,RcpAddition4,0,VARTYPEADD);
  addRcpVar(st,RcpAddition5,0,VARTYPEADD);
  addRcpVar(st,RcpWhirlpoolHoppingTemp,0,VARTYPENA);
  addRcpVar(st,RcpWhirlpoolHoppingTime,0,VARTYPENA);
  addRcpVar(st,RcpWhirlpoolTerminalTemp,0,VARTYPENA);
  addRcpVar(st,RcpWhirlpoolMinimumTime,0,VARTYPESIMPLE);
  addRcpVar(st,RcpWhirlpoolRestTime,0,VARTYPESIMPLE);
  addRcpVar(st,RcpInoculationTemp,1,VARTYPESIMPLE);
  addRcpVar(st,RcpKettleSourSouringTemp,1,VARTYPENA);
  addRcpVar(st,RcpKettleSourFirstBoilTime,0,VARTYPESIMPLE);
  strnncat(st,"}\n </script>\n",RECIPEDATAMAXSIZE);

  ethernetProcessString_and_Page(request,st,"/www/recipe.html");
}

char * getGambainoStatus(char *st) {
  char buf[2048];
  char smallBuf[600];
  NTPFormatedDateTime(smallBuf);
  snprintf(buf,199,"<BR>WiFi SSID: %s<BR>System time: %s<BR>",WiFi.SSID().c_str(),smallBuf);
  strnncat(st,buf,MAXSTATUSLEN);

  if (debugging)
    strnncat(st,"DEBUG mode<BR>",MAXSTATUSLEN);
  else
    strnncat(st,"Operational mode<BR>",MAXSTATUSLEN);

  snprintf(buf,199,"<br>Free memory: %i bytes - biggest segment: %i bytes<br>",ESP.getFreeHeap(),ESP.getMaxAllocHeap());
  strnncat(st,buf,MAXSTATUSLEN);

  snprintf(buf,199,"<br>Main task stack water mark: %i bytes<br>",uxTaskGetStackHighWaterMark(NULL));
  strnncat(st,buf,MAXSTATUSLEN);

  /*
  if (datalogTask) {
    snprintf(buf,199,"Log task stack water mark: %i bytes<br>",uxTaskGetStackHighWaterMark(datalogTask));
    strnncat(st,buf,MAXSTATUSLEN);
  }
    */

  snprintf(buf,199,"<br>SPIFFS use: %i/%i bytes<br>",SPIFFS.usedBytes(),SPIFFS.totalBytes());
  strnncat(st,buf,MAXSTATUSLEN);

  strnncat(st, getRecentAlertsHtml(smallBuf, sizeof(smallBuf)), MAXSTATUSLEN);

  yield();
  taskYIELD();

  strnncat(st,ProcVar::getClustersStatus(buf),MAXSTATUSLEN);

  yield();
  taskYIELD();

  if (valveCurrentMonitorIsOn) {
    snprintf(buf,1023,"<br><br>INA219 is ok.<br>\
- Shunt voltage: %.2f mV<br>\
- Bus voltage: %.2f v<br>\
- Power: %.2f mW<br>\
- Current: %.2f mA<br>\
- Normalized current: %.2f mA (threshold: %.2f mA)<br><br>",
              ina219.getShuntVoltage_mV(),ina219.getBusVoltage_V(),ina219.getPower_mW(),
              ina219.getCurrent_mA(),actualCurrent,valveThresholdCurrent);
    strnncat(st,buf,MAXSTATUSLEN);
  }
  else {
    Wire.beginTransmission(INA219_ADDRESS);
    if (Wire.endTransmission(true) != ESP_OK)
      strnncat(st,"<br><br>INA219 is not responding<br><br>",MAXSTATUSLEN);
    else
      strnncat(st,"<br><br>INA219 is connected, but not initialized<br><br>",MAXSTATUSLEN);
  }

  snprintf(buf,199,"<br>MLT bottom level reading: %d<br>",analogRead(PINMLTTOPLEVEL));
  strncat(st,buf,MAXSTATUSLEN);

  yield();
  taskYIELD();

  strnncat(st,ProcVar::getDallasStatus(buf,1,2040),MAXSTATUSLEN);
  strnncat(st,"<BR>",MAXSTATUSLEN);
  
  yield();
  taskYIELD();  
  
  strnncat(st,ProcVar::getDallasStatus(buf,2,2040),MAXSTATUSLEN);
  strnncat(st,"<BR>",MAXSTATUSLEN);
  

  yield();
  taskYIELD();

  getPeerStatus(st, MAXSTATUSLEN);

  return st;
}

void returnDataLogHeader(AsyncWebServerRequest * request) {
  char buf[2000];
  ProcVar::dataLogHeader(buf,2000);
  Serial.println(buf);
  request->send(200,"text/html",buf);
}


hw_timer_t *timer = NULL;

void sendDiagEmail(AsyncWebServerRequest *request) {
  sendSimpleMailFromFile("Gambaino Diagnostic","/out/diagnostics.html",true);
  request->send(200,"text/html", "email sent");
}

void safeModeCmd(AsyncWebServerRequest *request) {
  char out[50];
  safeMode  = ! safeMode;
  if (safeMode)
    strcpy(out, "Safe mode ON");
  else
    strcpy(out, "Safe mode OFF");

  SIPOutput(request,out);
}

void UI_SETUP() {
  cmdSerialBuf[0] = '\0';

  setupWiFi();
  loadPeers();
  registerOwnPeer(PEERTYPE_BREWCORE);

  // ESP-NOW: BrewCore doesn't use GLog ESP-NOW, so we init explicitly here.
  // initPeerEspNowReceive registers gambainoEspNowRecvCb for peer broadcast/reply packets.
  if (esp_now_init() == ESP_OK) {
    initPeerEspNowReceive();
    setExtraEspNowHandler(brewCoreHandleFermTemp);
  } else {
    Serial.println("BrewCore esp_now_init failed");
  }

  setStatusSource(getGambainoStatus);
  registerPeerSetupRoute();
  server.on("/cold",HTTP_GET, [](AsyncWebServerRequest *request){ColdSideWebPage(request);});
  server.on("/saveColdSide",HTTP_GET, [](AsyncWebServerRequest *request){saveColdSide(request);});
  server.on("/recipe",HTTP_GET, [](AsyncWebServerRequest *request){recipeWebPage(request);});
  server.on("/favicon",HTTP_GET, [](AsyncWebServerRequest *request){ethernetProcessPage(request,"/www/img/favicon.ico");});
  server.on("/gj_1",HTTP_GET, [](AsyncWebServerRequest *request){requestFromIP = true; JSONOutput(request, 1);});

  server.on("/diag",       HTTP_GET, [](AsyncWebServerRequest *request){ethernetProcessPage(request,"/www/diagParams.html");});
  server.on("/diagresult", HTTP_GET, [](AsyncWebServerRequest *request){ethernetProcessPage(request,"/out/diagnostics.html");});
  server.on("/calibrresult", HTTP_GET, [](AsyncWebServerRequest *request){calibrResultPage(request);});

  //server.on("/senddiag", HTTP_GET, [](AsyncWebServerRequest *request){sendDiagEmail(request);});
  server.on("/speed", HTTP_GET, [](AsyncWebServerRequest *request){speed(request);});
  server.on("/safemode", HTTP_GET, [](AsyncWebServerRequest *request){safeModeCmd(request);});
  server.on("/getdatalogheader", HTTP_GET, [](AsyncWebServerRequest *request){returnDataLogHeader(request);});

  server.onNotFound([](AsyncWebServerRequest *request){ethernetCmdReceived(request);});

  
  NTPBegin(-3);
  echoMode = 0;
   DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin","*");
}

void commandHelp(AsyncWebServerRequest *request) {
    char newLine[10];
    if (requestFromIP)
      strcpy(newLine, "<br>");
    else
      strcpy(newLine, "\n\r");
      
    strcpy (bigBuffer,"COMMAND LIST (commands are case insensitive)"                                                                                ); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"help | ? - override a variable with a new value. Works for both input and output variables"                   ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"StartProgram <program> - Start a Gambaino program. Program can be:"                                           ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...0 - stand by program / abort current program and release all overrides"                                    ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...1 - manual control"                                                                                        ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...2 - complete cycle - preclean + brew"                                                                      ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...3 - brew without preclean"                                                                                 ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...4 - preclean only"                                                                                         ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"FOrce <variable> <value> - override a variable with a new value. Works for both input and output variables"   ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"RElease <variable> - release the overrided value of a variable, returning to program defined/acquired value"  ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"ReleaseAll - release all overrided variables"                                                                 ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"ShowMath - show temperature and volume calculations"                                                          ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"LIst <group> - list variables, <group> can be:"                                                               ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...A - all variables"                                                                                         ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...O - overrided variables"                                                                                   ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...D - Dallas thermometers"                                                                                   ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...C - process control"                                                                                       ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...R - recipe"                                                                                                ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...L - level, volume and flow"                                                                                ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...H - heater and mash temperature parameters"                                                                ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...V - hot side valves"                                                                                       ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...P - hot side pumps"                                                                                        ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...T - hot side temperature"                                                                                  ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...X - chillers"                                                                                              ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...I - CIP related actuators"                                                                                 ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...S - Cold side temperature and control"                                                                     ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"GetJson <group> - returns JSON structure for variables. <group> can be:"                                      ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...1 - process control variables"                                                                             ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"...2 - recipe variables"                                                                                      ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"StatusList - returns a JSON structure with all possibile status names"                                        ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"IPCStatusList - returns a JSON structure with all possible IPC status names"                                  ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"ECho [ON | OFF] - echoes all network output to serial for debugging"                                          ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"MOnitor <variable> - start printing <variable> to serial every 500 ms"                                        ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"StopMonitor - stop variable monitoring if active"                                                             ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"i2c || iic - scan I2C devices, show I2C buses and their current value"                                        ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"ScanDallas - scan for Dallas thermometers"                                                                    ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"StopMonitor - stop variable monitoring if active"                                                             ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"reset - restart Gambaino firmware"                                                                            ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"NExt - go to next status when waiting for 'next button'"                                                      ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"GET <datalog group>[)ddmmyy] - download datalog (HTTP only)"                                                  ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"ListTodo - list all to do items"                                                                              ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"getTodoJson - get json with all active to do items"                                                           ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"DismissTodo <n> - dismiss item <n> in to do list"                                                             ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"SAVEprocess - save process state variables to EEPROM and suspend auto saving"                                 ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"ResumeProcess - restore process status from EEPROM"                                                           ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"FACTORYRESET - Restore factory defauts"                                                                       ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"COLDstatus - formated status of cold side (http only)"                                                        ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"STARTDIAG <parameters - bitmask> - start diagnostic"                                                          ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,""                                                                                                             ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);

    strnncat(bigBuffer,"DIRECT URLs (case senstive)"                                                                                  ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"cold - cold side status and configuration"                                                                    ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"recipe - recipe parameters"                                                                                   ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"diag - diagnostic parameters and execution"                                                                   ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"diagresult - diagnostic result"                                                                               ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,"senddiag - send diagnostic result via e-mail"                                                                 ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);  
    strnncat(bigBuffer,"speed - speed test"                                                                                           ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    strnncat(bigBuffer,""                                                                                                             ,BIGBUFFERSIZE); strnncat(bigBuffer,newLine,BIGBUFFERSIZE);
    
    SIPOutput(request,bigBuffer);
}

void calibrResultPage(AsyncWebServerRequest *request) {
  strcpy(bigBuffer,"<html><head><style>pre { white-space: pre-wrap; font-family: Arial, sans-serif; }</style></head><body>");
  strcat(bigBuffer,"<h2>Calibration results:</h2><pre>");
  getCalibration(bigBuffer + strlen(bigBuffer));
  strcat(bigBuffer,"</pre></body></html>");

  request->send(200,"text/html",bigBuffer);
}


void setAlertMessage(const char *msg)
{
    strcpy(alertMessage, msg);
}

void clearAlertMessage()
{
    alertMessage[0]=0;
}

char * getAlertMessage()
{
    if (alertMessage[0])
        return alertMessage;
    else
        return (char*)" ";
}

bool hasAlertMessage()
{
    return alertMessage[0] != 0;
}


void setConsoleMessage(const char * msg,...) {
  if (!msg) {
    consoleMessageNext[0] = ' ';
    consoleMessageNext[1] = 0;
  }
  else {
    va_list args;
    va_start(args, msg);      
    if (!consoleMessageNext[0] || !consoleMessageNext[1]) {
      vsnprintf(consoleMessageNext,119,msg,args);
    }
    else {
      char buf[120];
      vsnprintf(buf,119,msg,args);
      strcat(consoleMessageNext,"<br><br>");
      strcat(consoleMessageNext,buf);    
    }
    va_end(args);
  }
}

void clearConsoleMessage() {
  consoleMessageNext[0] = ' ';
  consoleMessageNext[1] = 0; 
}

void commitConsoleMessage() {
  strcpy(consoleMessage,consoleMessageNext);
}

void setCountDownMessage(const char * msg, float secs)
{
  int min = int(secs / 60);
  int sec = secs - min*60;
  char secStr[15];
  char minStr[15];

  sprintf(minStr,"%02d",min);
  sprintf(secStr,"%02d",sec);
 
  setConsoleMessage(msg,minStr,secStr);
}


char * getConsoleMessage(char *buf) {
  if (consoleMessage[0]) {
    strcpy(buf, consoleMessage);
    return buf;
  }
  else {
    buf[0] = 0;
    return (char*)"";
  }
}


void beep(int note, int duration)
{

  //Play tone on PINBUZZER
EasyBuzzer.setPin(PINBUZZER);
EasyBuzzer.beep(1000,10);
//tone(PINBUZZER,NOTE_E5);

  //tone(PINBUZZER, note, duration);  
  delay(duration);

EasyBuzzer.stopBeep();
pinMode(PINBUZZER,OUTPUT);
digitalWrite(PINBUZZER,LOW);


  //Stop tone on PINBUZZER
  noTone(PINBUZZER); 
//  digitalWrite(PINBUZZER,LOW);
 
 
}


#define SOUNDINTERVAL 2000
unsigned long int lastSound = 0;

void sound_Beep() {
  if (MILLISDIFF(lastSound,SOUNDINTERVAL)) {
pinMode(PINBUZZER,OUTPUT);
digitalWrite(PINBUZZER,LOW);
  EasyBuzzer.setPin(PINBUZZER);
 EasyBuzzer.beep(1000,10);    
delay(200);
EasyBuzzer.stopBeep();
pinMode(PINBUZZER,OUTPUT);
digitalWrite(PINBUZZER,LOW);

    lastSound = millis();
    //analogWrite(PINBUZZER,500);esp32
    delay(200);
    //esp32 analogWrite(PINBUZZER,0);
  }
}

void sound_ToDo() 
{
  if (MILLISDIFF(lastSound,SOUNDINTERVAL)) {
    lastSound = millis();
    beep(f, 150);  
    delay(20);
    beep(gS, 200);  
  }
}

void sound_Error()
{
  if (MILLISDIFF(lastSound,SOUNDINTERVAL)) {
    lastSound = millis();
    beep(aH, 80);  
    delay(30);
    beep(120, 500);
  }
}

void sound_Attention()
{
  if (MILLISDIFF(lastSound,SOUNDINTERVAL)) {
    lastSound = millis();
    beep(gS, 40);  
    delay(5);
    beep(eH, 130);  
    delay(5);
    beep(aH, 40);  
    Led2 = ON;
    Led2.setWithDelay(OFF,3);    
  }
}

void sound_smallChange()
{
  if (MILLISDIFF(lastSound,SOUNDINTERVAL)) {
    lastSound = millis();
 
    beep(eH,8);  
    delay(4);
    beep(e,8);
  }
}

void sound_ackOverride()
{
  if (MILLISDIFF(lastSound,SOUNDINTERVAL)) {
    lastSound = millis();
  
    beep(cH,30);  
    delay(8);
    beep(c,30);
  }
}

void sound_stateChange()
{
  if (MILLISDIFF(lastSound,SOUNDINTERVAL)) {
    lastSound = millis();
  
    beep(gS, 20);  
    delay(5);
    beep(eH, 20);  
    delay(5);
    beep(gH, 20);  
    delay(5);
    beep(aH, 60);  
  }
}


#define DIAGTDSTR "<td style='border: 1px solid black; padding:5px;'>"
#define DIAGTDSTRSPAN "<td colspan='4' style='border: 1px solid black; padding:5px;'>"

void diagEntry(byte level, const char * line, const char *result_, byte errLevel,...) {
  char buf[1001]="";
  va_list args;
  va_start(args,errLevel);
  char result[300] = "";
  if (result_ && result_[0])
    vsnprintf(result,299,result_,args);
  va_end(args);


  if (level==0) { // cabeçalho
    strnncat(buf,"<h1>",1000);    
    strnncat(buf,line,1000);    
    strnncat(buf,"</h1><table style='border: 1px solid black; border-collapse: collapse'>",1000);
  }
  else {
    if (level==1)
      strnncat(buf,"<tr>"  DIAGTDSTRSPAN "<b>",1000);
    else
      strnncat(buf,"<tr>" DIAGTDSTR,1000);

    strnncat(buf,line,1000);
    if (level==1)
      strnncat(buf,"</b>",1000);

    if (level==2) {
      strnncat(buf,"</td>" DIAGTDSTR,1000);
      strnncat(buf,result,1000);
      
      strnncat(buf,"</td>" DIAGTDSTR,1000);

      switch (errLevel) {
        case 0: strnncat(buf, "<font color='green'>OK</font>",1000); break;
        case 1: strnncat(buf, "<font color='orange'>warning</font>",1000); break;
        case 2: strnncat(buf, "<font color='red'>ERROR</font>",1000); break;
      }
    }
    strnncat(buf,"</td></tr>",1000);
  }

  File diagFile = SPIFFS.open("/out/diagnostics.html", "a"); 
  if (!diagFile) {
    alert("Firmware error: diagFile not open");
    return;
  }
  else {
    diagFile.println(buf);
    diagFile.close();
  }
}

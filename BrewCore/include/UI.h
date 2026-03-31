#ifndef UI_h
#define UI_h

#include <Arduino.h>
#include <ESPAsyncWebServer.h>



void UI_SETUP();


int ethernetPooling(char *cmdBuf);
int serialPooling(char *cmdBuf);

void cmdProcess(AsyncWebServerRequest *request,char *cmd);

void SIPOutput(AsyncWebServerRequest *request,const char *s, int beep=0);
void SIPOutput(AsyncWebServerRequest *request,char *s, int beep=0);

void setAlertMessage (const char *msg);
void clearAlertMessage();
char * getAlertMessage();
bool hasAlertMessage();

void setConsoleMessage (const char *msg,...);
void setCountDownMessage(const char* msg, float Secs);
void clearConsoleMessage();
void commitConsoleMessage();
char *getConsoleMessage(char *buf);

void sound_Beep();
void sound_ToDo();
void sound_Error();
void sound_Attention();
void sound_smallChange();
void sound_ackOverride();
void sound_stateChange();

void diagEntry(byte level, const char * line, const char *result = NULL, byte errLevel=0, ...);

#endif
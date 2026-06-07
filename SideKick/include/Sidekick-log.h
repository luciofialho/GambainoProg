#ifndef SIDEKICK_LOG_H
#define SIDEKICK_LOG_H
void cashLogRequest(char *logEntry);
void cashBrewfatherLogRequest(char *logEntry);
void sendLogToGoogleSheets();
void sendLogToBrewfather();
char * getLogStatus(char * st);
#endif

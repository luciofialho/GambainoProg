#ifndef SIDEKICK_LOG_H
#define SIDEKICK_LOG_H
void cashLogRequest(char *logEntry);
void sendLogToGoogleSheets();
char * getLogStatus(char * st);
#endif

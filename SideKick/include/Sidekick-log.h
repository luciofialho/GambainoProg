#ifndef SIDEKICK_LOG_H
#define SIDEKICK_LOG_H
// Call once in setup before starting the receivers and the LogSend task.
bool initLogQueues();
void cashLogRequest(const char *logEntry);
void cashBrewfatherLogRequest(const char *logEntry);
// Only the LogSend task may call these: it owns the retained retry payloads.
void sendLogToGoogleSheets();
void sendLogToBrewfather();
char * getLogStatus(char * st);
#endif

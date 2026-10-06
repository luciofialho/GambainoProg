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

// Brewfather stream URL, kept in NVS (default: BREWFATHER_STREAM_URL in the code).
#define BREWFATHER_URL_MAXLEN 200
void loadBrewfatherSettings();   // call in setup, before the LogSend task starts
void getBrewfatherStreamURL(char *buf, size_t size);
bool setBrewfatherStreamURL(const char *url);   // empty disables; false if invalid or not saved
#endif

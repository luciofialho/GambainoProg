#ifndef SIDEKICK_CLOUD_H
#define SIDEKICK_CLOUD_H
#include <stddef.h>

// Cloud log (Povoto/docs/cloud-log.md): records from the Povotos are spooled
// in LittleFS and posted to the cloud Worker, so an internet outage or a
// reboot does not lose them.

// Call once in setup, before the receivers and the LogSend task.
bool initCloudLog();
// Receive path (loop task): appends one record to the spool.
void cashCloudLogRecord(const char *record);
// Receive path (loop task): keeps only the latest batch state of each Povoto.
void cashCloudState(const char *state);
// Only the LogSend task may call this. It also polls the cloud every minute
// for requests to the Povotos (phase 2).
void sendCloudLog();
// loop(): sends one queued cloud request to its Povoto.
void forwardCloudCommands();
// Appends the cloud status lines (HTML) to st.
void appendCloudLogStatus(char *st, size_t size);
// Worker URL and SideKick token as fields of the Connection settings page.
void registerCloudLogSettings();
// Appends the "Site: <number> (<name>)" line of the token (the cloud defines it).
void appendCloudSiteStatus(char *st, size_t size);

#endif

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
// Only the LogSend task may call this.
void sendCloudLog();
// Appends the cloud status lines (HTML) to st.
void appendCloudLogStatus(char *st, size_t size);
// GET /cloud and POST /cloud/update: Worker URL and SideKick token.
void registerCloudLogRoutes();

#endif

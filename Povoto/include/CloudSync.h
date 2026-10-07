#ifndef POVOTO_CLOUD_SYNC_H
#define POVOTO_CLOUD_SYNC_H

#include <Arduino.h>

// Phase 2 of the cloud (docs/cloud-log.md, "Set points pela nuvem"): the
// Povoto owns its set points and automatic rules; the cloud keeps a copy
// made of snapshots sent from here, and sends requests that are applied only
// when they were made on the current version (hash).

void cloudSyncBegin();
// loop(): applies queued requests, sends snapshots when a hash changes.
void cloudSyncProcess();
// ESP-NOW receive path (Wi-Fi task): queues a request from the SideKick.
void cloudSyncReceive(const char *payload, const uint8_t *senderMac);

// "Accept cloud edits" (Settings page, NVS; on by default). Off: every request
// but a snapshot is refused.
bool cloudEditsAccepted();
bool setCloudEditsAccepted(bool accepted);

// CRC32 (hex, 8 chars) of the set point and rule snapshots, for the state line.
void cloudSetpointHash(char *out, size_t size);
void cloudRulesHash(char *out, size_t size);

// Diagnostics.
unsigned long cloudRequestsApplied();
unsigned long cloudRequestsRejected();
unsigned long cloudRequestsForeign(); // dropped: not from the paired SideKick

#endif

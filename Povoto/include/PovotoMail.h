#ifndef POVOTOMAIL_H
#define POVOTOMAIL_H

#include <Arduino.h>

// Queues a plain-text e-mail for the mail task and returns immediately.
// Returns false when the queue is full or the task could not be started.
bool queuePovotoMail(const char *subject, const String &body);

#endif // POVOTOMAIL_H

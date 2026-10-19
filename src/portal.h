#pragma once
#include <Arduino.h>

// Setup hotspot ("NECO-SETUP-xxxx", password = the board PIN) with a captive page,
// and the same page as a PIN-protected web admin at http://neco-cyd.local once online.
bool needsSetup();        // no wifi saved or no API key
void ensurePin();         // generate the 8-digit PIN on first boot
void portalRun();         // blocking: hotspot + screen; restarts after save, returns on SKIP
void adminBegin();        // start web admin + mDNS (safe to call repeatedly)
void adminLoop();
String apName();

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Neco's brain on the CYD: persona from neco-ai, live readings, DeepSeek API,
// and a reflections memory store compatible with neco-ai's `neco-memory.py export`.

struct BrainReply {
  bool ok = false;
  String text, mood = "normal", error;
};

void brainBegin();                         // mount LittleFS, load reflections
const char *catName();                     // NECO / CONECO / SAKAMOTO (display)
BrainReply brainChat(const String &msg);   // one conversational turn
BrainReply brainIdle();                    // a short idle thought for the home screen
int brainReflect();                        // consolidate the last exchange; returns memories added (-1 = skipped/failed)
void brainForgetHistory();                 // drop short-term chat context
String brainBodyNotes();                   // "about this body" facts in her prompt
bool brainSetBodyNotes(const String &t);   // saved to /body.md

// reflections store
int memCount();
int memActiveCount();
void memExport(Print &out);                // same JSON shape as neco-ai export
bool memImportFile(const char *path, String &msg);
bool memForget(int id);
void memClear();

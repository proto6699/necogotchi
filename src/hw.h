#pragma once
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "config.h"

extern TFT_eSPI tft;
extern Preferences prefs;

struct Settings {
  uint8_t brightness = 180;   // 10..255
  bool autoBright = false;
  uint8_t volume = 40;        // 0..100 (100 = ~35% of DAC swing, see hw.cpp)
  bool sounds = true;
  bool scanlines = true;
  bool flicker = true;
  bool motion = true;         // rolling band, specks, glitches
  bool invert = true;         // panel colour inversion (most new CYDs need it)
  uint8_t touchSens = 0;      // 0 soft, 1 normal, 2 firm
  bool repeatOne = false;     // music: repeat this track, else loop the whole card
  bool autoplay = true;       // music: start the playlist on boot when an SD card has songs
  uint8_t cat = 0;            // 0 Neco, 1 Coneco, 2 Sakamoto
  int8_t tzHours = 3;         // UTC offset
  String apiKey;              // DeepSeek API key
  String model = "deepseek-flash";
  String userName = "Echo";   // what she calls you
  uint8_t idleMin = 30;       // idle thought interval, 0 = off
  String pin;                 // setup hotspot password + web admin password
  // touch calibration (raw values at screen edges)
  // defaults = the usual ESP32-2432S028 layout (channels swapped vs the screen)
  uint16_t tx0 = 200, tx1 = 3700, ty0 = 240, ty1 = 3800;
  bool swapXY = true;
  bool calibrated = false;
};
extern Settings cfg;

void hwBegin();
void loadSettings();
void saveSettings();

// backlight + LED
void setBacklight(uint8_t v);
void applyBrightness();
void ledColor(uint8_t r, uint8_t g, uint8_t b);
void hwTick();  // flicker, auto-brightness, LED breathing

// touch
bool touchRaw(uint16_t &x, uint16_t &y, uint16_t &z);
void touchProbe(uint16_t &x, uint16_t &y, uint16_t &z);  // raw, no threshold
bool touchPoint(int16_t &x, int16_t &y);  // calibrated screen coords

// sd
bool sdReady();
bool sdMount();

// audio
enum Sfx { SFX_MEOW, SFX_PURR, SFX_BLIP, SFX_CHIRP };
void playSfx(Sfx s);
bool playFile(const String &path);   // mp3 / wav from SD
void audioStop();
bool audioBusy();
bool musicPlaying();
String nowPlaying();   // track name without extension, "" if nothing
void audioPause(bool p);
bool musicSuspend();  // stop decoding and free its RAM; true if a song was playing
void musicResume();   // continue that song from the same spot
void audioQuiet();  // flush DMA to silence before a long blocking call
bool audioPaused();
void audioLoop();
void applyVolume();

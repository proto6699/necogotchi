#pragma once
#include "hw.h"
#include "assets/font_vt323.h"

#define F_SMALL &VT16
#define F_BIG &VT24
#define BAR_H 20

struct Btn {
  int16_t x, y, w, h;
  const char *label;
  bool hit(int16_t px, int16_t py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

void uiTick();                                   // audio + flicker; call in every loop
void clearRect(int x, int y, int w, int h);      // Den background + scanlines
void clearScreen();
void text(int x, int y, const String &s, const GFXfont *f, uint16_t c, uint8_t datum = TL_DATUM);
void drawBtn(const Btn &b, bool active = false, uint16_t color = C_GREEN);
void frame(int x, int y, int w, int h, uint16_t c = C_LINE);
int wrapText(int x, int y, int w, const String &s, uint16_t c, int maxLines, int lineH = 17);
void statusBar(const char *title);
void toast(const String &msg, uint16_t c = C_AMBER, uint32_t ms = 1200);

// touch events
enum TouchEv { T_NONE, T_TAP, T_LONG };
TouchEv pollTouch(int16_t &x, int16_t &y);  // edge-triggered; LONG after 700 ms hold

// modal on-screen keyboard; returns false on cancel
bool keyboard(String &value, const char *title, bool masked = false, int maxLen = 96);

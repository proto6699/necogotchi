#include "ui.h"
#include <WiFi.h>
#include <time.h>
#include "fx.h"

void uiTick() {
  audioLoop();
  hwTick();
  fxTick();
}

void clearRect(int x, int y, int w, int h) {
  fxRestore();
  tft.fillRect(x, y, w, h, C_BG);
  if (!cfg.scanlines) return;
  for (int yy = y + ((3 - y % 3) % 3); yy < y + h; yy += 3) tft.drawFastHLine(x, yy, w, C_SCAN);
}

void clearScreen() { clearRect(0, 0, SCREEN_W, SCREEN_H); }

void text(int x, int y, const String &s, const GFXfont *f, uint16_t c, uint8_t datum) {
  fxRestore();
  tft.setFreeFont(f);
  tft.setTextColor(c);
  tft.setTextDatum(datum);
  tft.drawString(s, x, y);
}

void frame(int x, int y, int w, int h, uint16_t c) { fxRestore(); tft.drawRect(x, y, w, h, c); }

void drawBtn(const Btn &b, bool active, uint16_t color) {
  clearRect(b.x, b.y, b.w, b.h);
  if (active) tft.fillRect(b.x, b.y, b.w, b.h, C_PANEL);
  tft.drawRect(b.x, b.y, b.w, b.h, active ? C_AMBER : C_LINE);
  // corner ticks, like the Den's bracketed panels
  tft.drawFastHLine(b.x, b.y, 4, color); tft.drawFastVLine(b.x, b.y, 4, color);
  tft.drawFastHLine(b.x + b.w - 4, b.y + b.h - 1, 4, color);
  tft.drawFastVLine(b.x + b.w - 1, b.y + b.h - 4, 4, color);
  text(b.x + b.w / 2, b.y + b.h / 2 + 1, b.label, F_SMALL, active ? C_HOT : color, MC_DATUM);
}

int wrapText(int x, int y, int w, const String &s, uint16_t c, int maxLines, int lineH) {
  fxRestore();
  tft.setFreeFont(F_SMALL);
  tft.setTextColor(c);
  tft.setTextDatum(TL_DATUM);
  int lines = 0; String line, word;
  auto flush = [&]() {
    if (lines < maxLines) tft.drawString(line, x, y + lines * lineH);
    lines++; line = "";
  };
  for (unsigned i = 0; i <= s.length(); i++) {
    char ch = i < s.length() ? s[i] : ' ';
    if (ch == '\n' || ch == ' ') {
      String trial = line.length() ? line + " " + word : word;
      if (tft.textWidth(trial) > w && line.length()) { flush(); line = word; }
      else line = trial;
      word = "";
      if (ch == '\n') flush();
    } else word += ch;
  }
  if (line.length()) flush();
  return min(lines, maxLines);
}

static void wifiGlyph(int x, int y) {
  if (WiFi.status() != WL_CONNECTED) { text(x, y, "x", F_SMALL, C_RUST, TR_DATUM); return; }
  int rssi = WiFi.RSSI();
  int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 3;
    uint16_t c = i < bars ? C_GREEN : C_LINE;
    tft.fillRect(x - 16 + i * 4, y + 14 - h, 3, h, c);
  }
}

void statusBar(const char *title) {
  clearRect(0, 0, SCREEN_W, BAR_H);
  text(4, 2, title, F_SMALL, C_GREEN);
  struct tm t;
  if (getLocalTime(&t, 0)) {
    char b[8]; strftime(b, sizeof b, "%H:%M", &t);
    text(SCREEN_W / 2 + 40, 2, b, F_SMALL, C_HOT, TC_DATUM);
  }
  text(SCREEN_W - 46, 2, sdReady() ? "SD" : "--", F_SMALL, sdReady() ? C_DIM : C_LINE, TR_DATUM);
  wifiGlyph(SCREEN_W - 4, 2);
  tft.drawFastHLine(0, BAR_H - 1, SCREEN_W, C_LINE);
}

void toast(const String &msg, uint16_t c, uint32_t ms) {
  int w = min(300, (int)msg.length() * 9 + 24), h = 30;
  int x = (SCREEN_W - w) / 2, y = SCREEN_H - h - 6;
  fxRestore();
  tft.fillRect(x, y, w, h, C_PANEL);
  tft.drawRect(x, y, w, h, c);
  text(SCREEN_W / 2, y + h / 2 + 1, msg, F_SMALL, c, MC_DATUM);
  fxPause(true);  // a toast isn't part of the screen's redraw
  uint32_t t0 = millis();
  while (millis() - t0 < ms) { uiTick(); delay(5); }
  fxPause(false);
  clearRect(x, y, w, h);
}

// ---------------- touch events ----------------
TouchEv pollTouch(int16_t &x, int16_t &y) {
  static bool down = false, longFired = false;
  static uint32_t t0 = 0, lastUp = 0;
  static int16_t sx, sy;
  int16_t px, py;
  bool now = touchPoint(px, py);
  if (now && !down) {
    if (millis() - lastUp < 60) return T_NONE;  // debounce
    down = true; longFired = false; t0 = millis(); sx = px; sy = py;
  } else if (now && down) {
    if (!longFired && millis() - t0 > 700) { longFired = true; x = sx; y = sy; return T_LONG; }
  } else if (!now && down) {
    down = false; lastUp = millis();
    if (!longFired) { x = sx; y = sy; return T_TAP; }
  }
  return T_NONE;
}

// ---------------- keyboard ----------------
static const char *ROWS_ABC[3] = {"qwertyuiop", "asdfghjkl'", "zxcvbnm"};
static const char *ROWS_123[3] = {"1234567890", "-/:;()$&@\"", ".,?!_+=#%"};

bool keyboard(String &value, const char *title, bool masked, int maxLen) {
  const int KW = 32, KH = 36, KY = 62;
  int layer = 0; bool shift = false;
  String v = value;

  auto drawField = [&]() {
    clearRect(0, 22, SCREEN_W, 36);
    frame(4, 24, SCREEN_W - 8, 32, C_DIM);
    String shown = masked ? String() : v;
    if (masked) for (unsigned i = 0; i < v.length(); i++) shown += (i + 1 == v.length()) ? v[i] : '*';
    tft.setFreeFont(F_SMALL);
    while (tft.textWidth(shown) > SCREEN_W - 28 && shown.length()) shown.remove(0, 1);
    text(10, 32, shown + "_", F_SMALL, C_HOT);
  };
  auto keyLabel = [&](char c) {
    if (layer == 0 && shift && c >= 'a' && c <= 'z') c -= 32;
    return String(c);
  };
  auto drawKeys = [&]() {
    clearRect(0, KY, SCREEN_W, SCREEN_H - KY);
    const char **rows = layer ? ROWS_123 : ROWS_ABC;
    for (int r = 0; r < 3; r++) {
      int n = strlen(rows[r]);
      int off = r == 2 ? 48 : 0;
      for (int i = 0; i < n; i++) {
        String l = keyLabel(rows[r][i]);
        Btn b{(int16_t)(off + i * KW), (int16_t)(KY + r * KH), KW - 2, KH - 3, l.c_str()};
        drawBtn(b);
      }
    }
    Btn sh{0, KY + 2 * KH, 46, KH - 3, layer ? "" : (shift ? "SHF" : "shf")};
    if (!layer) drawBtn(sh, shift);
    drawBtn({272, KY + 2 * KH, 48, KH - 3, "DEL"}, false, C_AMBER);
    int y4 = KY + 3 * KH;
    drawBtn({0, (int16_t)y4, 46, KH - 6, layer ? "abc" : "123"});
    drawBtn({48, (int16_t)y4, 142, KH - 6, "space"});
    drawBtn({192, (int16_t)y4, 62, KH - 6, "ESC"}, false, C_RUST);
    drawBtn({256, (int16_t)y4, 64, KH - 6, "OK"}, false, C_LIME);
  };

  fxPause(true);  // the keyboard has no scene redraw
  clearScreen();
  statusBar(title);
  drawField();
  drawKeys();

  while (true) {
    uiTick();
    int16_t x, y;
    TouchEv ev = pollTouch(x, y);
    if (ev == T_NONE) { delay(4); continue; }
    if (y < KY) continue;
    int r = (y - KY) / KH;
    bool changedKeys = false;
    if (r < 3) {
      const char **rows = layer ? ROWS_123 : ROWS_ABC;
      int off = r == 2 ? 48 : 0;
      if (r == 2 && x < 46) { if (!layer) { shift = !shift; changedKeys = true; } }
      else if (r == 2 && x >= 272) { if (v.length()) v.remove(v.length() - 1); if (ev == T_LONG) v = ""; }
      else {
        int i = (x - off) / KW;
        if (i >= 0 && i < (int)strlen(rows[r]) && (int)v.length() < maxLen) {
          v += keyLabel(rows[r][i]);
          if (shift) { shift = false; changedKeys = true; }
        }
      }
    } else {
      if (x < 46) { layer = !layer; shift = false; changedKeys = true; }
      else if (x < 190) { if ((int)v.length() < maxLen) v += ' '; }
      else if (x < 254) { playSfx(SFX_BLIP); fxPause(false); return false; }
      else { value = v; playSfx(SFX_CHIRP); fxPause(false); return true; }
    }
    playSfx(SFX_BLIP);
    drawField();
    if (changedKeys) drawKeys();
  }
}

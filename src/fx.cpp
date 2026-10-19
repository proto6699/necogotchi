#include "fx.h"
#include "hw.h"

static const int BAND = 3;           // rows in the rolling bright band
static uint16_t bandBuf[SCREEN_W * BAND];
static int bandY = -1;               // -1 = nothing saved
static int rollY = 0;

static const int SPECKS = 10;
static int16_t spX[SPECKS], spY[SPECKS];
static uint16_t spC[SPECKS];
static bool specksOn = false;

static uint16_t glitchBuf[SCREEN_W * 8];
static bool paused = false;
static bool readOk = true;  // some panels can't be read back (the dual-USB CYD); then use redraw mode
// Redraw mode: no readback, so restore the band's rows by re-running the current screen's
// draw function clipped (viewport) to those rows. Main sets the function on every screen change.
static void (*scene)() = nullptr;
static bool inRedraw = false;
static int clipBandY = -1;
static const int CLIP_BAND = 2;
#define C_BAND rgb(0x2a, 0x5c, 0x34)

static void redrawRows(int y, int h) {
  if (!scene || inRedraw || y < 0) return;
  inRedraw = true;
  tft.setViewport(0, y, SCREEN_W, h, false);  // absolute coords, clipped to the rows
  scene();
  tft.resetViewport();
  inRedraw = false;
}

void fxSetScene(void (*fn)()) {
  if (clipBandY >= 0 && scene) redrawRows(clipBandY, CLIP_BAND);
  clipBandY = -1;
  scene = fn;
}
bool fxReadback() { return readOk; }
static uint32_t nextRoll = 0, nextSpeck = 0, nextGlitch = 0, nextCorner = 0;

// readRect() hands back byte-swapped 565 (ready for pushRect); unswap to edit
static inline uint16_t unswap(uint16_t c) { return (c << 8) | (c >> 8); }

// brighten toward phosphor: the band reads as a slightly hotter line of the tube
static uint16_t glow(uint16_t sw, int amt) {
  uint16_t c = unswap(sw);
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  r = min(31, r + amt / 2); g = min(63, g + amt + 2); b = min(31, b + amt / 2);
  uint16_t o = (r << 11) | (g << 5) | b;
  return unswap(o);
}

void fxRestore() {
  if (inRedraw) return;
  if (clipBandY >= 0) { int y = clipBandY; clipBandY = -1; redrawRows(y, CLIP_BAND); }
  if (bandY >= 0) { tft.pushRect(0, bandY, SCREEN_W, BAND, bandBuf); bandY = -1; }
  if (specksOn) {
    for (int i = 0; i < SPECKS; i++) tft.drawPixel(spX[i], spY[i], spC[i]);
    specksOn = false;
  }
}

bool fxSelfTest() {
  // write two colours, read them back; if the panel won't answer, motion fx would corrupt the screen
  uint16_t a = tft.readPixel(0, SCREEN_H - 1);
  tft.drawPixel(0, SCREEN_H - 1, C_AMBER);
  bool ok1 = tft.readPixel(0, SCREEN_H - 1) == C_AMBER;
  tft.drawPixel(0, SCREEN_H - 1, C_LIME);
  bool ok2 = tft.readPixel(0, SCREEN_H - 1) == C_LIME;
  tft.drawPixel(0, SCREEN_H - 1, a);
  readOk = ok1 && ok2;
  return readOk;
}

void fxPause(bool p) { if (p) fxRestore(); paused = p; }

void fxCorners() {
  const int R = 9;
  for (int i = 0; i < R; i++) {
    float dy = R - i - 0.5f;
    int w = R - (int)sqrtf(R * R - dy * dy);  // pixels outside the arc on this row
    if (w <= 0) continue;
    tft.drawFastHLine(0, i, w, TFT_BLACK);
    tft.drawFastHLine(SCREEN_W - w, i, w, TFT_BLACK);
    tft.drawFastHLine(0, SCREEN_H - 1 - i, w, TFT_BLACK);
    tft.drawFastHLine(SCREEN_W - w, SCREEN_H - 1 - i, w, TFT_BLACK);
  }
}

static void glitch() {
  int h = random(3, 8), y = random(20, SCREEN_H - 10), dx = random(3, 9) * (random(2) ? 1 : -1);
  tft.readRect(0, y, SCREEN_W, h, glitchBuf);
  // slide the strip sideways, fill the exposed edge with background
  for (int r = 0; r < h; r++) {
    if (dx > 0) tft.pushRect(dx, y + r, SCREEN_W - dx, 1, glitchBuf + r * SCREEN_W);
    else tft.pushRect(0, y + r, SCREEN_W + dx, 1, glitchBuf + r * SCREEN_W - dx);
  }
  if (dx > 0) tft.fillRect(0, y, dx, h, C_BG); else tft.fillRect(SCREEN_W + dx, y, -dx, h, C_BG);
  delay(random(40, 90));
  tft.pushRect(0, y, SCREEN_W, h, glitchBuf);
}

void fxTick() {
  uint32_t now = millis();
  if (now > nextCorner) { nextCorner = now + 400; fxCorners(); }
  if (paused || !cfg.motion) { if (bandY >= 0 || specksOn || clipBandY >= 0) fxRestore(); return; }

  if (!readOk) {  // redraw mode: rolling band only (specks/tears need readback)
    if (!scene || now < nextRoll) return;
    nextRoll = now + 45;
    if (clipBandY >= 0) { int y = clipBandY; clipBandY = -1; redrawRows(y, CLIP_BAND); }
    rollY += 3;
    if (rollY >= SCREEN_H + 140) rollY = 20;
    if (rollY >= 20 && rollY < SCREEN_H - CLIP_BAND) {
      tft.drawFastHLine(0, rollY, SCREEN_W, C_BAND);
      tft.drawFastHLine(0, rollY + 1, SCREEN_W, C_SCAN);
      clipBandY = rollY;
    }
    return;
  }

  if (now > nextRoll) {
    nextRoll = now + 28;
    if (bandY >= 0) { tft.pushRect(0, bandY, SCREEN_W, BAND, bandBuf); bandY = -1; }
    rollY += 2;
    if (rollY >= SCREEN_H + 120) rollY = 0;  // off-screen stretch = pause between sweeps
    if (rollY < SCREEN_H - BAND) {
      tft.readRect(0, rollY, SCREEN_W, BAND, bandBuf);
      static uint16_t lit[SCREEN_W * BAND];
      for (int i = 0; i < SCREEN_W * BAND; i++) lit[i] = glow(bandBuf[i], i / SCREEN_W == 1 ? 7 : 3);
      tft.pushRect(0, rollY, SCREEN_W, BAND, lit);
      bandY = rollY;
    }
  }

  if (now > nextSpeck) {
    nextSpeck = now + random(70, 160);
    if (specksOn) for (int i = 0; i < SPECKS; i++) tft.drawPixel(spX[i], spY[i], spC[i]);
    for (int i = 0; i < SPECKS; i++) {
      spX[i] = random(SCREEN_W);
      spY[i] = random(20, SCREEN_H);
      if (bandY >= 0 && spY[i] >= bandY && spY[i] < bandY + BAND) spY[i] = (spY[i] + 6) % SCREEN_H;
      spC[i] = tft.readPixel(spX[i], spY[i]);
      tft.drawPixel(spX[i], spY[i], random(4) ? C_DIM : C_HOT);
    }
    specksOn = true;
  }

  if (now > nextGlitch) {
    if (nextGlitch && !musicPlaying()) { fxRestore(); glitch(); }
    nextGlitch = now + random(15000, 45000);
  }
}

void fxPowerOn() {
  fxRestore();
  tft.fillScreen(TFT_BLACK);
  int cy = SCREEN_H / 2;
  for (int w = 4; w <= SCREEN_W; w += 26) {  // dot stretches into a line
    tft.drawFastHLine((SCREEN_W - w) / 2, cy, w, C_HOT);
    delay(8);
  }
  for (int h = 1; h <= SCREEN_H / 2; h += 12) {  // line opens into the picture
    tft.fillRect(0, cy - h, SCREEN_W, 2 * h, h < 40 ? C_HOT : C_GREEN);
    delay(10);
  }
  tft.fillScreen(C_BG);
  delay(40);
}

void fxPowerOff() {
  fxRestore();
  int cy = SCREEN_H / 2;
  for (int h = SCREEN_H / 2; h > 1; h -= 12) {  // picture collapses to a line
    tft.fillRect(0, 0, SCREEN_W, cy - h, TFT_BLACK);
    tft.fillRect(0, cy + h, SCREEN_W, SCREEN_H - cy - h, TFT_BLACK);
    tft.fillRect(0, cy - h, SCREEN_W, 2 * h, h < 30 ? C_HOT : C_GREEN);
    delay(12);
  }
  for (int w = SCREEN_W; w > 2; w -= 24) {  // line shrinks to a dot
    tft.drawFastHLine(0, cy, SCREEN_W, TFT_BLACK);
    tft.drawFastHLine(0, cy - 1, SCREEN_W, TFT_BLACK);
    tft.drawFastHLine((SCREEN_W - w) / 2, cy, w, C_HOT);
    delay(9);
  }
  for (int r = 3; r >= 0; r--) {  // dot fades
    tft.fillRect(SCREEN_W / 2 - 4, cy - 4, 9, 9, TFT_BLACK);
    if (r) tft.fillCircle(SCREEN_W / 2, cy, r, r > 1 ? C_HOT : C_DIM);
    delay(60);
  }
}

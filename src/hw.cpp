#include "hw.h"
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <driver/i2s.h>
#include <driver/dac.h>
#include <driver/rtc_io.h>
#include <AudioOutput.h>
#include <AudioGeneratorWAV.h>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourcePROGMEM.h>
#include "assets/sounds.h"

TFT_eSPI tft;
Preferences prefs;
Settings cfg;

// ---------------- settings ----------------
void loadSettings() {
  prefs.begin("neco", true);
  cfg.brightness = prefs.getUChar("bri", cfg.brightness);
  cfg.autoBright = prefs.getBool("abri", cfg.autoBright);
  cfg.volume = prefs.getUChar("vol", cfg.volume);
  cfg.sounds = prefs.getBool("snd", cfg.sounds);
  cfg.scanlines = prefs.getBool("scan", cfg.scanlines);
  cfg.flicker = prefs.getBool("flk", cfg.flicker);
  cfg.motion = prefs.getBool("mot", cfg.motion);
  cfg.invert = prefs.getBool("inv", cfg.invert);
  cfg.touchSens = prefs.getUChar("ts", cfg.touchSens);
  cfg.repeatOne = prefs.getBool("rep1", cfg.repeatOne);
  cfg.autoplay = prefs.getBool("auto", cfg.autoplay);
  cfg.cat = prefs.getUChar("cat", cfg.cat);
  cfg.tzHours = prefs.getChar("tz", cfg.tzHours);
  cfg.apiKey = prefs.getString("key", "");
  cfg.model = prefs.getString("model", cfg.model);
  cfg.userName = prefs.getString("uname", cfg.userName);
  cfg.idleMin = prefs.getUChar("idle", cfg.idleMin);
  cfg.pin = prefs.getString("pin", "");
  cfg.tx0 = prefs.getUShort("tx0", cfg.tx0);
  cfg.tx1 = prefs.getUShort("tx1", cfg.tx1);
  cfg.ty0 = prefs.getUShort("ty0", cfg.ty0);
  cfg.ty1 = prefs.getUShort("ty1", cfg.ty1);
  cfg.swapXY = prefs.getBool("sw", cfg.swapXY);
  // calibration format 2 = 4-corner; older saved calibrations are redone once
  cfg.calibrated = prefs.getBool("cal", false) && prefs.getUChar("calv", 0) >= 3;
  prefs.end();
}

void saveSettings() {
  prefs.begin("neco", false);
  prefs.putUChar("bri", cfg.brightness);
  prefs.putBool("abri", cfg.autoBright);
  prefs.putUChar("vol", cfg.volume);
  prefs.putBool("snd", cfg.sounds);
  prefs.putBool("scan", cfg.scanlines);
  prefs.putBool("flk", cfg.flicker);
  prefs.putBool("mot", cfg.motion);
  prefs.putBool("inv", cfg.invert);
  prefs.putUChar("ts", cfg.touchSens);
  prefs.putBool("rep1", cfg.repeatOne);
  prefs.putBool("auto", cfg.autoplay);
  prefs.putUChar("cat", cfg.cat);
  prefs.putChar("tz", cfg.tzHours);
  prefs.putString("key", cfg.apiKey);
  prefs.putString("model", cfg.model);
  prefs.putString("uname", cfg.userName);
  prefs.putUChar("idle", cfg.idleMin);
  prefs.putString("pin", cfg.pin);
  prefs.putUShort("tx0", cfg.tx0);
  prefs.putUShort("tx1", cfg.tx1);
  prefs.putUShort("ty0", cfg.ty0);
  prefs.putUShort("ty1", cfg.ty1);
  prefs.putBool("sw", cfg.swapXY);
  prefs.putBool("cal", cfg.calibrated);
  prefs.putUChar("calv", 3);
  prefs.end();
}

// ---------------- backlight / LED ----------------
static const int BL_CH = 0, LR_CH = 1, LG_CH = 2, LB_CH = 3;
static uint8_t blLevel = 180;

void setBacklight(uint8_t v) { blLevel = v; ledcWrite(BL_CH, v); }

void applyBrightness() {
  if (!cfg.autoBright) { setBacklight(max<uint8_t>(10, cfg.brightness)); return; }
  // LDR reads high in the dark on the CYD
  int raw = analogRead(PIN_LDR);
  int lvl = map(constrain(raw, 0, 1200), 0, 1200, 255, 25);
  setBacklight(lvl);
}

void ledColor(uint8_t r, uint8_t g, uint8_t b) {  // active low
  ledcWrite(LR_CH, 255 - r);
  ledcWrite(LG_CH, 255 - g);
  ledcWrite(LB_CH, 255 - b);
}

void hwTick() {
  static uint32_t nextFlicker = 0, lastAuto = 0;
  uint32_t now = millis();
  // CRT flicker: a rare, tiny dip in the backlight
  if (cfg.flicker && now > nextFlicker) {
    ledcWrite(BL_CH, blLevel * 0.72);
    delay(18);
    ledcWrite(BL_CH, blLevel);
    nextFlicker = now + random(4000, 15000);
  }
  if (cfg.autoBright && now - lastAuto > 1500) { lastAuto = now; applyBrightness(); }
}

// ---------------- touch (bit-banged XPT2046) ----------------
// The CYD's TFT owns HSPI and the SD card owns VSPI, so touch gets a software bus
// (the approach the CYD reference repo recommends). One chip-select per conversion.
static void touchPinsIdle() {
  pinMode(PIN_T_MOSI, OUTPUT);
  pinMode(PIN_T_MISO, INPUT);
  pinMode(PIN_T_CLK, OUTPUT);
  pinMode(PIN_T_CS, OUTPUT);
  pinMode(PIN_T_IRQ, INPUT);
  digitalWrite(PIN_T_CS, HIGH);
  digitalWrite(PIN_T_CLK, LOW);
}

static uint16_t xptRead(uint8_t cmd) {
  digitalWrite(PIN_T_CS, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(PIN_T_MOSI, (cmd >> i) & 1);
    digitalWrite(PIN_T_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(PIN_T_CLK, LOW);  delayMicroseconds(2);
  }
  uint16_t v = 0;
  for (int i = 0; i < 16; i++) {
    digitalWrite(PIN_T_CLK, HIGH); delayMicroseconds(2);
    v = (v << 1) | (digitalRead(PIN_T_MISO) ? 1 : 0);
    digitalWrite(PIN_T_CLK, LOW);  delayMicroseconds(2);
  }
  digitalWrite(PIN_T_CS, HIGH);
  return (v >> 3) & 0x0FFF;  // first slot is the busy bit, then D11..D0
}

static const uint8_t CMD_Z1 = 0xB1, CMD_Z2 = 0xC1, CMD_X = 0xD1, CMD_Y = 0x91;

// pressure: only meaningful when z2 > z1 > 0; idle noise reads ~10-20, a press 2000+
static uint16_t xptPressure() {
  uint16_t z1 = xptRead(CMD_Z1), z2 = xptRead(CMD_Z2);
  return (z1 > 0 && z2 > z1) ? z1 + 4095 - z2 : 0;
}

void touchProbe(uint16_t &x, uint16_t &y, uint16_t &z) {
  z = xptPressure();
  x = xptRead(CMD_X);
  y = xptRead(CMD_Y);
}

static const int Z_START[3] = {250, 350, 600};  // soft / normal / firm

static uint16_t median(uint16_t *v, int n) {
  for (int i = 1; i < n; i++) { uint16_t k = v[i]; int j = i - 1; while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; } v[j + 1] = k; }
  return v[n / 2];
}

bool touchRaw(uint16_t &x, uint16_t &y, uint16_t &z) {
  static bool held = false;
  uint16_t p = xptPressure();
  bool irq = digitalRead(PIN_T_IRQ) == LOW;  // pen-down line from the XPT2046
  int thr = Z_START[cfg.touchSens % 3];
  held = held ? (irq || p > thr / 2) : (irq || p > thr);
  if (!held) return false;
  const int N = 7;
  uint16_t xs[N], ys[N];
  for (int i = 0; i < N; i++) { xs[i] = xptRead(CMD_X); ys[i] = xptRead(CMD_Y); delayMicroseconds(150); }
  x = median(xs, N); y = median(ys, N); z = p;
  return true;
}

bool touchPoint(int16_t &x, int16_t &y) {
  static float fx = -1, fy = -1;
  uint16_t rx, ry, rz;
  if (!touchRaw(rx, ry, rz)) { fx = fy = -1; return false; }
  if (cfg.swapXY) { uint16_t t = rx; rx = ry; ry = t; }
  // tx0/tx1 may be reversed (tx0 > tx1); map() handles the flip
  int sx = constrain((int)map(rx, cfg.tx0, cfg.tx1, 0, SCREEN_W), 0, SCREEN_W - 1);
  int sy = constrain((int)map(ry, cfg.ty0, cfg.ty1, 0, SCREEN_H), 0, SCREEN_H - 1);
  if (fx < 0) { fx = sx; fy = sy; } else { fx += (sx - fx) * 0.5f; fy += (sy - fy) * 0.5f; }
  x = (int16_t)fx; y = (int16_t)fy;
  return true;
}

// ---------------- SD ----------------
static bool sdOk = false;
bool sdReady() { return sdOk; }
bool sdMount() {
  SPI.begin(18, 19, 23, PIN_SD_CS);
  sdOk = SD.begin(PIN_SD_CS, SPI, 20000000);
  return sdOk;
}

// ---------------- audio ----------------
// Own built-in-DAC output instead of ESP8266Audio's AudioOutputI2S, because on the CYD:
//  - the speaker amp is on GPIO26 = DAC channel 2 = I2S "LEFT" (the enum names are crossed)
//  - GPIO25 = DAC channel 1 is the touch clock; installing I2S in DAC mode claims it, and
//    AudioOutputI2S re-claims both pads (i2s_set_pin NULL) on every playback
//  - DAC silence is mid-scale (0x8000), not zero; zeros put 0 V on the amp = loud pops
//  - the DAC needs MSB framing
// So: install once at boot, give GPIO25 back once, park at mid-scale, never re-install.
static const int DMA_BUFS = 12, DMA_LEN = 256;

class CydDac : public AudioOutput {
 public:
  bool init() {
    i2s_config_t c = {};
    c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    c.sample_rate = 16000;
    c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    c.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    c.dma_buf_count = DMA_BUFS;
    c.dma_buf_len = DMA_LEN;
    c.tx_desc_auto_clear = false;
    c.use_apll = false;
    if (i2s_driver_install(I2S_NUM_0, &c, 0, nullptr) != ESP_OK) { Serial.println("[audio] i2s install failed"); return false; }
    i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);   // GPIO26, the speaker
    dac_output_disable(DAC_CHANNEL_1);           // GPIO25 ...
    rtc_gpio_deinit(GPIO_NUM_25);                // ... back to the GPIO matrix for touch
    rate = 16000; up = true;
    idle(pdMS_TO_TICKS(200));  // every DMA buffer at mid-scale before anything plays
    silent = true;
    return true;
  }
  bool SetRate(int hz) override {
    if (up && hz > 0 && hz != rate) { i2s_set_sample_rates(I2S_NUM_0, hz); rate = hz; }
    return AudioOutput::SetRate(hz);
  }
  bool begin() override { return up; }
  bool ConsumeSample(int16_t s[2]) override {
    if (!up) return false;
    MakeSampleStereo16(s);
    int32_t m = ((int32_t)s[0] + s[1]) / 2;
    m = (int32_t)(m * level);
    if (m > 32767) m = 32767; else if (m < -32767) m = -32767;
    silent = false;
    uint16_t w = (uint16_t)(m + 0x8000);  // offset binary: 0x8000 = silence
    uint16_t frame[2] = {w, w};
    size_t written = 0;
    i2s_write(I2S_NUM_0, frame, sizeof frame, &written, 0);
    return written == sizeof frame;
  }
  bool stop() override { if (!silent) idle(pdMS_TO_TICKS(60)); silent = true; return true; }
  void setLevel(float l) { level = l; }
 private:
  // With auto-clear off, a starved DMA replays its last buffers forever: a few ms of old
  // audio on loop = a whistle. So silence means overwriting EVERY buffer with mid-scale.
  void idle(TickType_t wait) {
    static uint16_t mid[DMA_LEN * 2];
    if (mid[0] != 0x8000) for (auto &v : mid) v = 0x8000;
    for (int i = 0; i < DMA_BUFS + 1; i++) { size_t w = 0; i2s_write(I2S_NUM_0, mid, sizeof mid, &w, wait); }
  }
  bool up = false, silent = false;
  int rate = 0;
  float level = 0.1f;
};

static CydDac *out = nullptr;
static AudioGenerator *gen = nullptr;
static AudioFileSource *src = nullptr;
static bool isMusic = false, paused = false;

// Volume 0..100 maps to at most ~35% of DAC swing: the CYD's 8002 amp is loud on a small speaker.
void applyVolume() {
  if (out) out->setLevel(cfg.volume / 100.0f * 0.35f);
}

static void freeAll() {
  if (gen) { if (gen->isRunning()) gen->stop(); delete gen; gen = nullptr; }
  if (src) { delete src; src = nullptr; }
  isMusic = paused = false;
}

static bool startGen(AudioGenerator *g, AudioFileSource *s, bool music) {
  freeAll();
  if (!out) { delete g; delete s; return false; }
  gen = g; src = s; isMusic = music;
  applyVolume();
  bool ok = gen->begin(src, out);
  if (!ok) freeAll();
  return ok;
}

void playSfx(Sfx s) {
  if (!cfg.sounds || isMusic) return;  // never interrupt music with UI sounds
  const uint8_t *d; size_t n;
  switch (s) {
    case SFX_MEOW:  d = snd_meow;  n = sizeof(snd_meow);  break;
    case SFX_PURR:  d = snd_purr;  n = sizeof(snd_purr);  break;
    case SFX_CHIRP: d = snd_chirp; n = sizeof(snd_chirp); break;
    default:        d = snd_blip;  n = sizeof(snd_blip);  break;
  }
  startGen(new AudioGeneratorWAV(), new AudioFileSourcePROGMEM(d, n), false);
}

static String curPath;

bool playFile(const String &path) {
  if (!sdOk) return false;
  curPath = path;
  String p = path; p.toLowerCase();
  AudioGenerator *g = p.endsWith(".wav") ? (AudioGenerator *)new AudioGeneratorWAV()
                                         : (AudioGenerator *)new AudioGeneratorMP3();
  return startGen(g, new AudioFileSourceSD(path.c_str()), true);
}

// Free the decoder for a big allocation (TLS), then pick the song up where it was.
static uint32_t suspendPos = 0;
static String suspendPath;
bool musicSuspend() {
  if (!musicPlaying() || paused || !src) return false;
  suspendPos = src->getPos();
  suspendPath = curPath;
  freeAll();
  if (out) out->stop();
  return true;
}
void musicResume() {
  if (!suspendPath.length() || !playFile(suspendPath)) return;
  if (src && suspendPos) src->seek(suspendPos, SEEK_SET);  // MP3 resyncs on the next frame
  suspendPath = "";
}

String nowPlaying() {
  if (!musicPlaying()) return "";
  String n = curPath.substring(curPath.lastIndexOf('/') + 1);
  int dot = n.lastIndexOf('.');
  return dot > 0 ? n.substring(0, dot) : n;
}

void audioStop() { freeAll(); if (out) out->stop(); }
bool audioBusy() { return gen && gen->isRunning(); }
bool musicPlaying() { return isMusic && gen && gen->isRunning(); }
void audioPause(bool p) { paused = p; if (p && out) out->stop(); }
// silence the DMA before something blocks the loop (HTTPS, Wi-Fi), so it can't loop a buffer
void audioQuiet() { if (out) out->stop(); }
bool audioPaused() { return paused; }

void audioLoop() {
  if (!gen || paused) return;
  if (gen->isRunning() && !gen->loop()) { gen->stop(); out->stop(); }
}

// ---------------- init ----------------
void hwBegin() {
  ledcSetup(BL_CH, 5000, 8); ledcAttachPin(PIN_BACKLIGHT, BL_CH);
  ledcSetup(LR_CH, 5000, 8); ledcAttachPin(PIN_LED_R, LR_CH);
  ledcSetup(LG_CH, 5000, 8); ledcAttachPin(PIN_LED_G, LG_CH);
  ledcSetup(LB_CH, 5000, 8); ledcAttachPin(PIN_LED_B, LB_CH);
  ledColor(0, 0, 0);
  setBacklight(0);

  touchPinsIdle();

  pinMode(PIN_LDR, INPUT);
  analogSetPinAttenuation(PIN_LDR, ADC_0db);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(cfg.invert);
  // dual-USB CYD gamma fix (witnessmenow/ESP32-Cheap-Yellow-Display cyd.md): washed-out greys otherwise
  tft.writecommand(0x26); tft.writedata(2); delay(120);
  tft.writecommand(0x26); tft.writedata(1);
  tft.fillScreen(C_BG);

  out = new CydDac();
  if (!out->init()) { delete out; out = nullptr; }
  applyVolume();
  touchPinsIdle();  // again, after audio claimed and released the DAC pads
  sdMount();
}

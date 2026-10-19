// neco-cyd: Neco's pocket body for the ESP32 "Cheap Yellow Display"
// Den palette + VT323, Wi-Fi, SD music player, brightness, keyboard,
// touch calibration, and a chat client for the BC-250 gateway.
#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <time.h>
#include "hw.h"
#include "ui.h"
#include "net.h"
#include "fx.h"
#include "brain.h"
#include "portal.h"
#include "assets/sprites.h"

// TLS to api.deepseek.com runs on the loop task; the default 8 KB stack is tight for it
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

enum Screen { S_HOME, S_CHAT, S_MUSIC, S_WIFI, S_SETTINGS, S_CALIBRATE };
static Screen screen = S_HOME;

// ======================================================= cats
static const char *CAT_NAMES[] = {"NECO", "CONECO", "SAKAMOTO"};
enum Mood { M_NORMAL, M_SLEEP, M_YAY };

static const uint16_t *catSprite(Mood m) {
  switch (cfg.cat % 3) {
    case 0: return m == M_SLEEP ? neco_sleep : m == M_YAY ? neco_yay : neco_normal;
    case 1: return coneco_normal;
    default: return m == M_SLEEP ? sakamoto_sleep : sakamoto_normal;
  }
}

static Mood moodFromString(const String &s) {
  if (s == "sleep" || s == "sleepy" || s == "tired") return M_SLEEP;
  if (s == "yay" || s == "happy" || s == "smug" || s == "excited") return M_YAY;
  return M_NORMAL;
}

static void drawCat(int x, int y, Mood m) {
  fxRestore();
  tft.setSwapBytes(true);
  tft.pushImage(x, y, CAT_SPRITE, CAT_SPRITE, catSprite(m));
  tft.setSwapBytes(false);
  // Den-style bracket corners
  int s = CAT_SPRITE + 4, x0 = x - 2, y0 = y - 2;
  tft.drawFastHLine(x0, y0, 10, C_AMBER); tft.drawFastVLine(x0, y0, 10, C_AMBER);
  tft.drawFastHLine(x0 + s - 10, y0 + s - 1, 10, C_AMBER); tft.drawFastVLine(x0 + s - 1, y0 + s - 10, 10, C_AMBER);
  // light scanlines over the sprite so it sits "inside" the CRT
  if (cfg.scanlines) for (int yy = y; yy < y + CAT_SPRITE; yy += 3) tft.drawFastHLine(x, yy, CAT_SPRITE, C_SCAN);
}

// LED mood light on the back (kept dim)
static void ledIdle() {
  float b = (sin(millis() / 1600.0) + 1) / 2;
  if (musicPlaying()) ledColor(14 * b + 2, 9 * b + 1, 0);
  else if (!netConnected()) ledColor(6 * b + 1, 0, 0);
  else ledColor(0, 10 * b + 1, 3 * b);
}

// ======================================================= offline lines
// Short, in Neco's voice (lowercase, dry, no emojis). Used when no gateway reply.
static String idleLine() {
  struct tm t;
  bool haveTime = getLocalTime(&t, 0);
  int h = haveTime ? t.tm_hour : -1;
  if (h >= 0 && h < 5) {
    const char *night[] = {"it's late. you're up. naturally.", "the moon is out and so are you.",
                           "nocturnal mode confirmed. not judging. mostly."};
    return night[random(3)];
  }
  if (!netConnected()) {
    const char *off[] = {"no wifi. i can still judge you locally.", "offline. running on vibes and 320 kilobytes.",
                         "the bc-250 is home. i'm the outdoor version."};
    return off[random(3)];
  }
  if (musicPlaying()) {
    const char *mus[] = {"this one's fine. perchance.", "the speaker is tiny. the effort is not.",
                         "i rate this track: audible."};
    return mus[random(3)];
  }
  if (!cfg.apiKey.length()) return "no api key yet. i'm just a face for now.";
  const char *gen[] = {"meowdy.", "tap chat if you want the real me.", "the bag is dark. i'm fine with that.",
                       "small screen, same cat.", "i was a lab subject. now i'm a pocket accessory. growth."};
  return gen[random(5)];
}

// ======================================================= HOME
static Mood homeMood = M_NORMAL;
static String homeLine;
static String thoughtAt;   // "thought 23:41" when homeLine is a real idle thought
static String thinkErr;    // why the last idle thought failed, shown small
static bool thinkNow = false, thinkForce = false;  // tap her words = think now
static uint32_t lastInteract = 0, lastLine = 0, moodUntil = 0, lastBarDraw = 0;
static const Btn DOCK[4] = {{2, 202, 77, 36, "CHAT"}, {82, 202, 77, 36, "MUSIC"},
                           {162, 202, 77, 36, "WIFI"}, {242, 202, 76, 36, "SET"}};

static void homeDrawText() {
  clearRect(126, 26, 194, 172);
  text(130, 28, CAT_NAMES[cfg.cat % 3], F_BIG, C_HOT);
  const char *ms = homeMood == M_SLEEP ? "asleep" : homeMood == M_YAY ? "pleased" : "idle";
  text(130, 58, String("mood: ") + ms, F_SMALL, C_DIM);
  tft.drawFastHLine(130, 78, 182, C_LINE);
  if (thoughtAt.length()) text(130, 82, thoughtAt, F_SMALL, C_DIM);
  wrapText(130, thoughtAt.length() ? 100 : 84, 184, homeLine, C_GREEN, thoughtAt.length() ? 4 : 5);
  if (thinkErr.length()) { String e = thinkErr; if (e.length() > 30) e = e.substring(0, 29) + "."; text(130, 164, e, F_SMALL, C_RUST); }
  if (musicPlaying()) {
    text(130, 180, audioPaused() ? "|| paused" : "> playing", F_SMALL, C_AMBER);
  }
}

static void homeEnter() {
  clearScreen();
  statusBar("NECO//DEN");
  drawCat(14, 32, homeMood);
  homeDrawText();
  for (auto &b : DOCK) drawBtn(b);
}

static void homeSetMood(Mood m) {
  if (m == homeMood) return;
  homeMood = m;
  drawCat(14, 32, homeMood);
  homeDrawText();
}

// hold the top bar: CRT power-off, backlight off; any tap brings her back
static void screenOff() {
  playSfx(SFX_BLIP);
  fxPowerOff();
  setBacklight(0);
  ledColor(0, 0, 0);
  int16_t x, y;
  while (touchPoint(x, y)) { uiTick(); delay(10); }  // let go of the long press
  fxPause(true);
  while (!touchPoint(x, y)) { audioLoop(); netTick(); delay(20); }  // music keeps playing
  while (touchPoint(x, y)) { audioLoop(); delay(10); }
  applyBrightness();
  fxPowerOn();
  fxPause(false);
  lastInteract = millis();
  homeEnter();
}

static void homeLoop() {
  uint32_t now = millis();
  ledIdle();
  if (now - lastBarDraw > 20000) { lastBarDraw = now; statusBar("NECO//DEN"); }
  if (moodUntil && now > moodUntil) { moodUntil = 0; homeSetMood(M_NORMAL); }
  if (homeMood != M_SLEEP && now - lastInteract > 90000) {
    homeLine = "zz."; thoughtAt = "";
    homeMood = M_NORMAL;  // force redraw
    homeSetMood(M_SLEEP);
  }
  // idle thoughts from DeepSeek; canned offline lines otherwise
  static uint32_t lastThought = 0;
  if (thinkNow) { thinkNow = false; lastThought = 0; lastInteract = now; }
  bool canThink = (cfg.idleMin || thinkForce) && cfg.apiKey.length() && netConnected();
  if (thinkForce && !canThink) {
    thinkForce = false;
    thinkErr = !cfg.apiKey.length() ? "no api key" : "no wifi";
    homeDrawText();
  }
  if (canThink && homeMood != M_SLEEP && (lastThought ? now - lastThought > cfg.idleMin * 60000UL : now > 60000 || thinkForce)) {
    lastThought = now; lastLine = now; thinkForce = false;
    clearRect(130, 180, 186, 18);
    text(130, 180, "...", F_SMALL, C_DIM);
    audioQuiet();
    BrainReply r = brainIdle();
    if (r.ok) {
      homeLine = r.text;
      thinkErr = "";
      struct tm t; char b[24] = "thought";
      if (getLocalTime(&t, 0)) strftime(b, sizeof b, "thought  %H:%M", &t);
      thoughtAt = b;
      homeMood = M_NORMAL;
      Mood m = moodFromString(r.mood);
      if (m == M_SLEEP) m = M_NORMAL;  // an idle thought shouldn't put her to sleep
      homeDrawText();
      homeSetMood(m);
      if (m == M_YAY) moodUntil = now + 4000;
      playSfx(SFX_CHIRP);
    } else { thinkErr = r.error; homeDrawText(); }
  }
  if (!canThink && homeMood != M_SLEEP && now - lastLine > 45000) { lastLine = now; homeLine = idleLine(); thoughtAt = ""; homeDrawText(); }

  int16_t x, y;
  TouchEv ev = pollTouch(x, y);
  if (ev == T_NONE) return;
  lastInteract = now;
  if (ev == T_LONG && y < BAR_H + 6) { screenOff(); return; }
  if (homeMood == M_SLEEP) { homeLine = "...what. i'm up."; thoughtAt = ""; homeMood = M_YAY; homeSetMood(M_NORMAL); playSfx(SFX_CHIRP); return; }

  if (x < 124 && y > 26 && y < 140) {  // the cat
    thoughtAt = "";
    if (ev == T_LONG) { playSfx(SFX_PURR); homeLine = "fine. that's acceptable."; }
    else { playSfx(SFX_MEOW); homeLine = idleLine(); }
    homeMood = M_NORMAL;
    homeSetMood(M_YAY);
    moodUntil = now + 2500;
    return;
  }
  if (x >= 126 && y > 26 && y < 196 && ev == T_TAP) {  // tap her words: ask for a thought now
    thinkNow = thinkForce = true; playSfx(SFX_BLIP); return;
  }
  for (int i = 0; i < 4; i++) if (DOCK[i].hit(x, y)) {
    playSfx(SFX_BLIP);
    static const Screen dest[4] = {S_CHAT, S_MUSIC, S_WIFI, S_SETTINGS};
    screen = dest[i];
  }
}

// ======================================================= CHAT
struct ChatLine { String who, text; };
static ChatLine history[6];
static int histN = 0;
static String chips[3];
static int nChips = 0;
static Mood chatMood = M_NORMAL;
static const Btn CHAT_TYPE{2, 206, 76, 32, "TYPE"}, CHAT_BACK{262, 206, 56, 32, "BACK"};

static void pushHistory(const String &who, const String &t) {
  if (histN == 6) { for (int i = 1; i < 6; i++) history[i - 1] = history[i]; histN = 5; }
  history[histN++] = {who, t};
}

static void chatDraw() {
  clearScreen();
  statusBar(cfg.apiKey.length() ? "CHAT//DEEPSEEK" : "CHAT//OFFLINE");
  // newest at the bottom, fill upward
  int y = 196;
  tft.setFreeFont(F_SMALL);
  for (int i = histN - 1; i >= 0 && y > 26; i--) {
    bool me = history[i].who == "you";
    // estimate height: wrap into a scratch count first
    String s = (me ? "> " : "") + history[i].text;
    int lines = max(1, (int)(tft.textWidth(s) / 300) + 1);
    lines = min(lines, 5);
    int h = lines * 17;
    if (y - h < 24) break;
    y -= h;
    wrapText(8, y, 304, s, me ? C_DIM : C_GREEN, lines);
    y -= 4;
  }
  if (!histN) {
    wrapText(10, 40, 300, cfg.apiKey.length()
        ? "type something. she thinks through deepseek and keeps her reflections on this board."
        : "no api key yet. SET > MORE > setup portal, then add your deepseek key from your phone.", C_DIM, 4);
  }
  tft.drawFastHLine(0, 202, SCREEN_W, C_LINE);
  drawBtn(CHAT_TYPE, false, C_LIME);
  for (int i = 0; i < nChips; i++) {
    String c = chips[i]; if (c.length() > 7) c = c.substring(0, 7);
    Btn b{(int16_t)(80 + i * 60), 206, 58, 32, c.c_str()};
    drawBtn(b, false, C_AMBER);
  }
  drawBtn(CHAT_BACK);
}

static void chatSend(const String &msg) {
  pushHistory("you", msg);
  chatDraw();
  clearRect(0, 180, SCREEN_W, 20);
  text(8, 182, catName() + String(" is thinking..."), F_SMALL, C_AMBER);
  ledColor(0, 6, 12);
  audioQuiet();
  BrainReply r = brainChat(msg);
  if (!r.ok) {
    pushHistory("sys", "[" + r.error + "]");
    chatDraw();
    playSfx(SFX_BLIP);
    return;
  }
  pushHistory("cat", r.text);
  chatMood = moodFromString(r.mood);
  chatDraw();
  playSfx(r.mood == "yay" ? SFX_MEOW : SFX_CHIRP);
  // reflection runs after you can already read the reply
  clearRect(200, 2, 70, 16);
  text(266, 2, "reflecting", F_SMALL, C_DIM, TR_DATUM);
  audioQuiet();
  int added = brainReflect();
  statusBar(cfg.apiKey.length() ? "CHAT//DEEPSEEK" : "CHAT//OFFLINE");
  if (added > 0) toast(added == 1 ? "she'll remember that" : "she noted " + String(added) + " things", C_DIM, 900);
}

static void chatEnter() {
  if (!nChips) { chips[0] = "hey"; chips[1] = "u good?"; chips[2] = "night"; nChips = 3; }
  chatDraw();
}

static void chatLoop() {
  int16_t x, y;
  if (pollTouch(x, y) == T_NONE) return;
  if (CHAT_BACK.hit(x, y)) { playSfx(SFX_BLIP); screen = S_HOME; return; }
  if (CHAT_TYPE.hit(x, y)) {
    String msg;
    if (keyboard(msg, "SAY SOMETHING", false, 160) && msg.length()) chatSend(msg);
    else chatDraw();
    return;
  }
  for (int i = 0; i < nChips; i++) {
    Btn b{(int16_t)(80 + i * 60), 206, 58, 32, ""};
    if (b.hit(x, y)) { chatSend(chips[i]); return; }
  }
}

// ======================================================= MUSIC
// Plays every .mp3/.wav on the SD card (all folders), loops the whole card or one track.
static const int MAX_TRACKS = 128;
static String tracks[MAX_TRACKS];
static int nTracks = 0, curTrack = -1, listTop = 0, failStreak = 0;
static bool wantPlaying = false, scanned = false;
static const int ROWS = 7, ROW_H = 21, LIST_Y = 24;
static const Btn MB_PREV{2, 202, 46, 36, "|<"}, MB_PLAY{50, 202, 54, 36, ">||"}, MB_NEXT{106, 202, 46, 36, ">|"},
                 MB_REP{154, 202, 52, 36, "ALL"}, MB_VDN{208, 202, 32, 36, "-"}, MB_VUP{242, 202, 32, 36, "+"},
                 MB_BACK{276, 202, 42, 36, "<<"}, MB_UP{290, 24, 28, 70, "^"}, MB_DN{290, 120, 28, 70, "v"};

static bool isAudio(const String &n) {
  String l = n; l.toLowerCase();
  return l.endsWith(".mp3") || l.endsWith(".wav");
}

static void scanDir(const String &path, int depth) {
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f && nTracks < MAX_TRACKS; f = dir.openNextFile()) {
    String n = f.name();
    n = n.substring(n.lastIndexOf('/') + 1);  // core 2.x returns the bare name; be safe
    String full = path + (path.endsWith("/") ? "" : "/") + n;
    bool isDir = f.isDirectory();
    f.close();
    if (n.startsWith(".") || n.equalsIgnoreCase("System Volume Information")) continue;
    if (isDir) { if (depth < 4) scanDir(full, depth + 1); }
    else if (isAudio(n)) tracks[nTracks++] = full;
  }
  dir.close();
}

static void scanTracks() {
  nTracks = 0; scanned = true;
  if (!sdReady() && !sdMount()) return;
  scanDir("/", 0);
  // stable order: by path, so folders play album by album
  for (int i = 1; i < nTracks; i++) { String k = tracks[i]; int j = i - 1;
    while (j >= 0 && tracks[j].compareTo(k) > 0) { tracks[j + 1] = tracks[j]; j--; } tracks[j + 1] = k; }
}

static String baseName(const String &p) { return p.substring(p.lastIndexOf('/') + 1); }

static void musicDrawList() {
  clearRect(0, LIST_Y, 288, ROWS * ROW_H + 4);
  if (!nTracks) {
    wrapText(8, 40, 276, sdReady() ? "no .mp3 or .wav on the card. any folder works."
                                   : "no sd card. insert a fat32 card with songs on it.", C_DIM, 4);
    return;
  }
  for (int i = 0; i < ROWS && listTop + i < nTracks; i++) {
    int idx = listTop + i, y = LIST_Y + i * ROW_H;
    bool cur = idx == curTrack;
    if (cur) tft.fillRect(2, y, 284, ROW_H - 1, C_PANEL);
    String n = baseName(tracks[idx]);
    tft.setFreeFont(F_SMALL);
    while (tft.textWidth(n) > 250 && n.length()) n.remove(n.length() - 1);
    text(8, y + 1, (cur ? "> " : "  ") + n, F_SMALL, cur ? C_HOT : C_GREEN);
  }
}

static void musicDrawStatus() {
  clearRect(0, 180, SCREEN_W, 20);
  String s = curTrack >= 0 ? (audioPaused() ? "paused" : musicPlaying() ? "playing" : "stopped") : String(nTracks) + " songs";
  text(8, 182, s, F_SMALL, C_AMBER);
  text(312, 182, String(cfg.autoplay ? "auto  " : "") + "vol " + String(cfg.volume), F_SMALL, C_DIM, TR_DATUM);
}

static void musicDrawButtons() {
  Btn rep = MB_REP; rep.label = cfg.repeatOne ? "ONE" : "ALL";
  for (auto b : {MB_PREV, MB_PLAY, MB_NEXT, rep, MB_VDN, MB_VUP, MB_BACK}) drawBtn(b);
}

static void musicDraw() {
  clearScreen();
  statusBar("MUSIC//SD");
  musicDrawList();
  drawBtn(MB_UP); drawBtn(MB_DN);
  musicDrawStatus();
  musicDrawButtons();
}

static void musicEnter() {
  if (!scanned) scanTracks();
  if (curTrack >= 0) listTop = max(0, curTrack - curTrack % ROWS);
  musicDraw();
}

static void playIndex(int i) {
  if (!nTracks) return;
  curTrack = (i % nTracks + nTracks) % nTracks;
  audioPause(false);
  wantPlaying = true;
  if (playFile(tracks[curTrack])) failStreak = 0;
  else failStreak++;  // musicBackground() moves on to the next file
}

// keeps the playlist going on every screen; loops forever
static void musicBackground() {
  if (!wantPlaying || audioPaused() || musicPlaying() || !nTracks) return;
  if (failStreak >= nTracks) { wantPlaying = false; return; }  // nothing on the card will play
  playIndex(cfg.repeatOne && failStreak == 0 ? curTrack : curTrack + 1);
  if (screen == S_MUSIC) { musicDrawList(); musicDrawStatus(); }
}

static void musicAutostart() {
  if (!cfg.autoplay) return;
  scanTracks();
  if (nTracks) playIndex(0);
}

static void musicLoop() {
  int16_t x, y;
  TouchEv ev = pollTouch(x, y);
  if (ev == T_NONE) return;
  if (MB_BACK.hit(x, y)) { screen = S_HOME; return; }
  if (MB_UP.hit(x, y)) { listTop = max(0, listTop - ROWS); musicDrawList(); return; }
  if (MB_DN.hit(x, y)) { if (listTop + ROWS < nTracks) listTop += ROWS; musicDrawList(); return; }
  if (MB_PREV.hit(x, y)) playIndex(curTrack - 1);
  else if (MB_NEXT.hit(x, y)) playIndex(curTrack + 1);
  else if (MB_PLAY.hit(x, y)) {
    if (ev == T_LONG) { audioStop(); wantPlaying = false; }  // hold = stop
    else if (curTrack < 0) playIndex(0);
    else if (musicPlaying()) audioPause(!audioPaused());
    else playIndex(curTrack);
  } else if (MB_REP.hit(x, y)) {
    if (ev == T_LONG) cfg.autoplay = !cfg.autoplay;  // hold = autoplay on boot on/off
    else cfg.repeatOne = !cfg.repeatOne;
    saveSettings(); musicDrawButtons();
  } else if (MB_VDN.hit(x, y)) { cfg.volume = max(0, cfg.volume - 10); applyVolume(); saveSettings(); }
  else if (MB_VUP.hit(x, y)) { cfg.volume = min(100, cfg.volume + 10); applyVolume(); saveSettings(); }
  else if (y >= LIST_Y && y < LIST_Y + ROWS * ROW_H && x < 288) {
    int idx = listTop + (y - LIST_Y) / ROW_H;
    if (idx < nTracks) playIndex(idx);
  } else return;
  musicDrawList();
  musicDrawStatus();
}

// ======================================================= WIFI
static int nScan = 0, scanTop = 0;
static const Btn WB_SCAN{2, 206, 76, 32, "SCAN"}, WB_FORGET{80, 206, 96, 32, "FORGET"}, WB_BACK{262, 206, 56, 32, "BACK"};

static void wifiDraw() {
  clearScreen();
  statusBar("WIFI");
  if (netConnected()) {
    text(8, 24, "on " + WiFi.SSID(), F_SMALL, C_HOT);
    text(312, 24, WiFi.localIP().toString(), F_SMALL, C_DIM, TR_DATUM);
  } else text(8, 24, savedCount() ? "not connected (" + String(savedCount()) + " saved)" : "not connected", F_SMALL, C_RUST);
  tft.drawFastHLine(0, 44, SCREEN_W, C_LINE);
  if (nScan <= 0) wrapText(8, 54, 300, nScan == 0 ? "tap SCAN." : "scanning...", C_DIM, 2);
  for (int i = 0; i < 7 && scanTop + i < nScan; i++) {
    int k = scanTop + i, y = 48 + i * 22;
    String s = WiFi.SSID(k); if (s.length() > 24) s = s.substring(0, 24);
    bool saved = isSaved(WiFi.SSID(k));
    text(8, y, (saved ? "* " : "  ") + s, F_SMALL, saved ? C_HOT : C_GREEN);
    String meta = String(WiFi.RSSI(k)) + (WiFi.encryptionType(k) == WIFI_AUTH_OPEN ? " open" : " lock");
    text(312, y, meta, F_SMALL, C_DIM, TR_DATUM);
  }
  tft.drawFastHLine(0, 202, SCREEN_W, C_LINE);
  drawBtn(WB_SCAN); drawBtn(WB_FORGET, false, C_RUST); drawBtn(WB_BACK);
  if (nScan > 7) drawBtn({180, 206, 78, 32, "MORE"});
}

static void wifiScan() {
  nScan = -1; scanTop = 0;
  wifiDraw();
  WiFi.scanDelete();
  audioQuiet();
  nScan = max(0, (int)WiFi.scanNetworks(false));
  wifiDraw();
}

static void wifiEnter() { wifiScan(); }

static void wifiLoop() {
  int16_t x, y;
  if (pollTouch(x, y) == T_NONE) return;
  if (WB_BACK.hit(x, y)) { screen = S_HOME; return; }
  if (WB_SCAN.hit(x, y)) { wifiScan(); return; }
  if (WB_FORGET.hit(x, y)) { forgetAll(); toast("saved networks cleared", C_RUST); wifiDraw(); return; }
  if (nScan > 7 && x >= 180 && x < 258 && y >= 206) { scanTop = scanTop + 7 < nScan ? scanTop + 7 : 0; wifiDraw(); return; }
  if (y >= 48 && y < 48 + 7 * 22) {
    int k = scanTop + (y - 48) / 22;
    if (k >= nScan) return;
    String ssid = WiFi.SSID(k), pass;
    bool open = WiFi.encryptionType(k) == WIFI_AUTH_OPEN;
    if (!open && !isSaved(ssid, &pass)) {
      if (!keyboard(pass, ("PASS: " + ssid).c_str(), true, 63)) { wifiDraw(); return; }
    }
    clearScreen(); statusBar("WIFI");
    text(160, 110, "connecting...", F_BIG, C_AMBER, MC_DATUM);
    audioQuiet();
    if (connectNow(ssid, pass)) {
      saveNetwork(ssid, pass);
      playSfx(SFX_CHIRP);
      toast("connected. saved.", C_LIME);
    } else {
      toast("failed. wrong pass?", C_RUST, 1600);
    }
    nScan = max(0, (int)WiFi.scanComplete());
    wifiScan();
  }
}

// ======================================================= SETTINGS
static int setPage = 0;
static const Btn SB_PAGE{180, 206, 78, 32, "MORE"}, SB_BACK{262, 206, 56, 32, "BACK"};
#define ROW(i) (26 + (i) * 34)

static void settingRow(int i, const String &label, const String &val) {
  int y = ROW(i);
  clearRect(0, y, SCREEN_W, 32);
  text(8, y + 8, label, F_SMALL, C_GREEN);
  drawBtn({150, (int16_t)y, 40, 30, "<"});
  text(240, y + 8, val, F_SMALL, C_HOT, TC_DATUM);
  drawBtn({280, (int16_t)y, 38, 30, ">"});
}

static void actionRow(int i, const String &label, const String &val) {
  int y = ROW(i);
  clearRect(0, y, SCREEN_W, 32);
  text(8, y + 8, label, F_SMALL, C_GREEN);
  String v = val; if (v.length() > 20) v = v.substring(0, 18) + "..";
  drawBtn({150, (int16_t)y, 168, 30, v.c_str()}, false, C_AMBER);
}

static const char *onoff(bool b) { return b ? "on" : "off"; }

static void settingsDraw() {
  clearScreen();
  static const char *T[3] = {"SET//BODY", "SET//LINK", "SET//SCREEN"};
  statusBar(T[setPage]);
  if (setPage == 0) {
    settingRow(0, "brightness", cfg.autoBright ? "auto" : String(cfg.brightness * 100 / 255) + "%");
    settingRow(1, "volume", String(cfg.volume) + "%");
    settingRow(2, "cat", CAT_NAMES[cfg.cat % 3]);
    settingRow(3, "crt fx", String(cfg.scanlines ? "lines" : "") + (cfg.scanlines && cfg.flicker ? "+" : "") +
                            (cfg.flicker ? "flick" : "") + (!cfg.scanlines && !cfg.flicker ? "off" : ""));
    settingRow(4, "sounds", onoff(cfg.sounds));
  } else if (setPage == 2) {
    static const char *SENS[3] = {"soft", "normal", "firm"};
    settingRow(0, "invert color", onoff(cfg.invert));
    settingRow(1, "touch", SENS[cfg.touchSens % 3]);
    actionRow(2, "touch", "calibrate");
    settingRow(3, "motion fx", !cfg.motion ? "off" : fxReadback() ? "full" : "band");
    settingRow(4, "utc offset", (cfg.tzHours >= 0 ? "+" : "") + String(cfg.tzHours));
  } else {
    actionRow(0, "setup portal", "open hotspot");
    text(8, ROW(1) + 8, "web admin", F_SMALL, C_GREEN);
    text(150, ROW(1) + 8, netConnected() ? "neco-cyd.local" : "(offline)", F_SMALL, C_HOT);
    text(8, ROW(2) + 8, "pin  " + cfg.pin, F_SMALL, C_AMBER);
    text(150, ROW(2) + 8, netConnected() ? WiFi.localIP().toString() : "", F_SMALL, C_DIM);
    settingRow(3, "idle thought", cfg.idleMin ? String(cfg.idleMin) + "m" : "off");
    text(8, ROW(4) + 8, String(memActiveCount()) + " reflections  " + (cfg.apiKey.length() ? "key ok" : "no key"), F_SMALL, C_DIM);
  }
  tft.drawFastHLine(0, 202, SCREEN_W, C_LINE);
  drawBtn({180, 206, 78, 32, setPage == 2 ? "<<" : "MORE>"});
  drawBtn(SB_BACK);
}

static void settingsEnter() { setPage = 0; settingsDraw(); }

static void settingsLoop() {
  int16_t x, y;
  if (pollTouch(x, y) == T_NONE) return;
  if (SB_BACK.hit(x, y)) { saveSettings(); screen = S_HOME; return; }
  if (SB_PAGE.hit(x, y)) { setPage = (setPage + 1) % 3; settingsDraw(); return; }
  if (y < ROW(0) || y >= ROW(5)) return;
  int row = (y - ROW(0)) / 34;
  int dir = x >= 280 ? 1 : (x >= 150 && x < 190) ? -1 : 0;
  playSfx(SFX_BLIP);
  if (setPage == 2) {
    if (row == 2) { if (x >= 150) { saveSettings(); screen = S_CALIBRATE; return; } return; }
    if (!dir) return;
    if (row == 0) { cfg.invert = !cfg.invert; tft.invertDisplay(cfg.invert); }
    else if (row == 1) cfg.touchSens = (cfg.touchSens + 3 + dir) % 3;
    else if (row == 3) cfg.motion = !cfg.motion;
    else if (row == 4) { cfg.tzHours = constrain(cfg.tzHours + dir, -12, 14); configTime(cfg.tzHours * 3600, 0, "pool.ntp.org"); }
  } else if (setPage == 0) {
    if (!dir) return;
    switch (row) {
      case 0: {  // 10% steps, past the ends toggles auto
        if (cfg.autoBright) { cfg.autoBright = false; cfg.brightness = dir > 0 ? 25 : 255; }
        else {
          int v = cfg.brightness + dir * 25;
          if (v > 255 || v < 10) cfg.autoBright = true; else cfg.brightness = v;
        }
        applyBrightness();
        break;
      }
      case 1: cfg.volume = constrain(cfg.volume + dir * 10, 0, 100); applyVolume(); break;
      case 2: cfg.cat = (cfg.cat + 3 + dir) % 3; homeMood = M_NORMAL; break;
      case 3: {  // cycle: both -> lines -> flicker -> off
        int st = (cfg.scanlines ? 2 : 0) + (cfg.flicker ? 1 : 0);
        st = (st + 4 + (dir > 0 ? -1 : 1)) % 4;
        cfg.scanlines = st & 2; cfg.flicker = st & 1;
        break;
      }
      case 4: cfg.sounds = !cfg.sounds; break;
    }
  } else {
    switch (row) {
      case 0: if (x >= 150) { saveSettings(); portalRun(); netBegin(); } break;  // returns only on SKIP
      case 3: if (dir) { static const uint8_t opts[4] = {0, 15, 30, 60}; int i = 0; while (i < 3 && opts[i] != cfg.idleMin) i++;
                         cfg.idleMin = opts[(i + 4 + dir) % 4]; } break;
    }
  }
  saveSettings();
  settingsDraw();
}

// ======================================================= CALIBRATE
// Waits for a press, averages raw samples, then waits for a clean release (150 ms of nothing).
// Shows live pressure/raw values so a dead touch chip is obvious.
static bool readTouchAvg(uint16_t &ax, uint16_t &ay) {
  uint16_t x, y, z;
  uint32_t lastShow = 0;
  while (!touchRaw(x, y, z)) {
    audioLoop();
    if (millis() - lastShow > 150) {
      lastShow = millis();
      uint16_t rx, ry, rz;
      touchProbe(rx, ry, rz);  // no threshold: shows light contact too
      clearRect(20, 176, 280, 18);
      text(160, 176, rz ? "pressure " + String(rz) + "  raw " + String(rx) + "," + String(ry) : "waiting for touch",
           F_SMALL, C_LINE, TC_DATUM);
    }
    delay(5);
  }
  delay(40);
  uint32_t sx = 0, sy = 0; int n = 0;
  for (int i = 0; i < 24; i++) { if (touchRaw(x, y, z)) { sx += x; sy += y; n++; } delay(4); }
  uint32_t freeSince = millis();
  while (millis() - freeSince < 150) { if (touchRaw(x, y, z)) freeSince = millis(); delay(5); }  // debounced release
  if (n < 8) return false;  // brushed it, not a press
  ax = sx / n; ay = sy / n;
  return true;
}

static void crosshair(int x, int y, uint16_t c) {
  tft.drawFastHLine(x - 10, y, 21, c);
  tft.drawFastVLine(x, y - 10, 21, c);
  tft.drawCircle(x, y, 5, c);
}

// 4-corner calibration: top-left, top-right, bottom-right, bottom-left.
// Moving along an edge changes only one screen axis, which tells us whether the panel's
// raw X/Y are swapped; averaging two corners per edge cancels tilt.
// No sounds here: the speaker DAC shares a pin with the touch clock.
static void calibrate() {
  fxPause(true);
  const int M = 24;
  const int TX[4] = {M, SCREEN_W - M, SCREEN_W - M, M};
  const int TY[4] = {M, M, SCREEN_H - M, SCREEN_H - M};
  bool sndWas = cfg.sounds; cfg.sounds = false;
  audioStop();
  int failures = 0;
  while (true) {
    clearScreen();
    text(160, 80, "TOUCH CALIBRATION", F_BIG, C_HOT, MC_DATUM);
    text(160, 112, "tap the centre of each cross", F_SMALL, C_DIM, MC_DATUM);
    text(160, 130, "4 corners, firm tap, then lift", F_SMALL, C_DIM, MC_DATUM);
    uint16_t rx[4], ry[4];
    for (int i = 0; i < 4; i++) {
      crosshair(TX[i], TY[i], C_AMBER);
      while (true) {
        if (!readTouchAvg(rx[i], ry[i])) continue;
        // a corner must be far (in raw units) from every corner already taken
        bool tooClose = false;
        for (int j = 0; j < i; j++) if (abs((int)rx[i] - rx[j]) + abs((int)ry[i] - ry[j]) < 500) tooClose = true;
        if (!tooClose) break;
        clearRect(20, 150, 280, 18);
        text(160, 150, "same spot as before? tap the amber cross", F_SMALL, C_RUST, TC_DATUM);
      }
      clearRect(20, 150, 280, 18);
      crosshair(TX[i], TY[i], C_LIME);
    }
    int hx = abs((int)rx[1] - rx[0]) + abs((int)rx[2] - rx[3]);
    int hy = abs((int)ry[1] - ry[0]) + abs((int)ry[2] - ry[3]);
    bool swap = hy > hx;
    uint16_t sx_[4], sy_[4];
    for (int i = 0; i < 4; i++) { sx_[i] = swap ? ry[i] : rx[i]; sy_[i] = swap ? rx[i] : ry[i]; }
    float left = (sx_[0] + sx_[3]) / 2.0f, right = (sx_[1] + sx_[2]) / 2.0f;
    float top = (sy_[0] + sy_[1]) / 2.0f, bottom = (sy_[2] + sy_[3]) / 2.0f;
    if (fabsf(right - left) >= 600 && fabsf(bottom - top) >= 450) {
      float kx = (right - left) / (SCREEN_W - 2 * M), ky = (bottom - top) / (SCREEN_H - 2 * M);
      cfg.swapXY = swap;
      cfg.tx0 = constrain((int)(left - kx * M), 0, 4095);
      cfg.tx1 = constrain((int)(right + kx * M), 0, 4095);
      cfg.ty0 = constrain((int)(top - ky * M), 0, 4095);
      cfg.ty1 = constrain((int)(bottom + ky * M), 0, 4095);
      break;
    }
    failures++;
    clearScreen();
    text(160, 40, "that didn't line up", F_BIG, C_RUST, MC_DATUM);
    // show what was read, so a photo of this screen says what's wrong
    for (int i = 0; i < 4; i++) {
      static const char *nm[4] = {"top-left", "top-right", "bottom-right", "bottom-left"};
      text(40, 70 + i * 20, String(nm[i]), F_SMALL, C_DIM);
      text(280, 70 + i * 20, String(rx[i]) + ", " + String(ry[i]), F_SMALL, C_HOT, TR_DATUM);
    }
    if (failures >= 3) {
      text(160, 160, "using default calibration for now", F_SMALL, C_AMBER, TC_DATUM);
      text(160, 180, "(redo it later in SET)", F_SMALL, C_DIM, TC_DATUM);
      cfg.swapXY = true; cfg.tx0 = 200; cfg.tx1 = 3700; cfg.ty0 = 240; cfg.ty1 = 3800;
      uint32_t t0 = millis(); while (millis() - t0 < 5000) { audioLoop(); delay(5); }
      break;
    }
    text(160, 170, "tap anywhere to try again", F_SMALL, C_AMBER, TC_DATUM);
    uint16_t a, b; readTouchAvg(a, b);
  }

  // verify: dots should land under your stylus
  clearScreen();
  text(160, 26, "draw to test", F_SMALL, C_DIM, TC_DATUM);
  Btn done{60, 200, 90, 34, "DONE"}, redo{170, 200, 90, 34, "REDO"};
  drawBtn(done, false, C_LIME);
  drawBtn(redo, false, C_AMBER);
  bool again = false;
  uint32_t t0 = millis();
  while (true) {
    audioLoop();
    int16_t x, y;
    if (touchPoint(x, y)) {
      t0 = millis();
      if (done.hit(x, y)) break;
      if (redo.hit(x, y)) { again = true; break; }
      if (y < 195) tft.fillCircle(x, y, 2, C_GREEN);
    }
    if (millis() - t0 > 60000) break;  // nobody's touching it: keep what we have
    delay(5);
  }
  int16_t x, y; while (touchPoint(x, y)) delay(10);
  cfg.sounds = sndWas;
  fxPause(false);
  if (again) { calibrate(); return; }
  cfg.calibrated = true;
  saveSettings();
  playSfx(SFX_CHIRP);
}

// ======================================================= BOOT
static void bootSequence() {
  applyBrightness();
  fxPowerOn();
  clearScreen();
  const char *lines[] = {"ECH0 BIOS v" FW_VERSION, "probing body....... cyd/esp32", "phosphor........... ok",
                         nullptr, "loading resident.... ", nullptr};
  int y = 20;
  for (int i = 0; i < 6; i++) {
    String l;
    if (i == 3) l = String("sd card............ ") + (sdReady() ? "mounted" : "none");
    else if (i == 5) l = String(CAT_NAMES[cfg.cat % 3]) + " is here.";
    else l = lines[i];
    if (i == 4) l += CAT_NAMES[cfg.cat % 3];
    for (unsigned c = 1; c <= l.length(); c++) {
      clearRect(10, y, 300, 18);
      text(10, y, l.substring(0, c), F_SMALL, i == 5 ? C_HOT : C_GREEN);
      delay(6);
    }
    y += 22;
    delay(120);
  }
  playSfx(SFX_MEOW);
  uint32_t t0 = millis();
  while (millis() - t0 < 700) { uiTick(); delay(5); }
}

// ======================================================= main
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  loadSettings();
  hwBegin();
  if (!fxSelfTest()) Serial.println("[fx] panel readback failed, motion effects off");
  brainBegin();
  ensurePin();
  netBegin();
  bootSequence();
  if (!cfg.calibrated) calibrate();
  if (needsSetup()) { portalRun(); netBegin(); }  // phone setup; SKIP = offline pet
  musicAutostart();
  homeLine = idleLine();
  lastInteract = lastLine = millis();
  homeEnter();
  fxSetScene(homeEnter);
}

void loop() {
  static Screen shown = S_HOME;
  uiTick();
  if (screen != S_WIFI) netTick();  // auto-connect would clear the scan list mid-browse
  if (netConnected()) { adminBegin(); adminLoop(); }
  musicBackground();

  if (screen != shown) {
    if (screen == S_CALIBRATE) { calibrate(); screen = S_SETTINGS; }
    shown = screen;
    lastInteract = millis();
    static void (*const SCENES[])() = {homeEnter, chatDraw, musicDraw, wifiDraw, settingsDraw};
    fxSetScene(nullptr);
    switch (screen) {
      case S_HOME: homeEnter(); break;
      case S_CHAT: chatEnter(); break;
      case S_MUSIC: musicEnter(); break;
      case S_WIFI: wifiEnter(); break;
      case S_SETTINGS: settingsEnter(); break;
      default: break;
    }
    if (screen <= S_SETTINGS) fxSetScene(SCENES[screen]);
  }
  switch (screen) {
    case S_HOME: homeLoop(); break;
    case S_CHAT: chatLoop(); break;
    case S_MUSIC: musicLoop(); break;
    case S_WIFI: wifiLoop(); break;
    case S_SETTINGS: settingsLoop(); break;
    default: break;
  }
  delay(2);
}

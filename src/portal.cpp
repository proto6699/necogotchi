#include "portal.h"
#include "hw.h"
#include "ui.h"
#include "net.h"
#include "brain.h"
#include "fx.h"
#include "portal_html.h"
#include "qr/qrcode_rm.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
using fs::File;
#include <ArduinoJson.h>

static WebServer server(80);
static DNSServer dns;
static bool setupMode = false, serverUp = false, savedFlag = false;
static File upload;

bool needsSetup() { return savedCount() == 0 || cfg.apiKey.length() == 0; }

void ensurePin() {
  if (cfg.pin.length() == 8) return;
  char b[9]; snprintf(b, sizeof b, "%08lu", (unsigned long)(esp_random() % 100000000UL));
  cfg.pin = b;
  saveSettings();
}

String apName() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char b[24]; snprintf(b, sizeof b, "NECO-SETUP-%02X%02X", mac[4], mac[5]);
  return b;
}

// ---------------- handlers ----------------
static bool authed() {
  if (setupMode) return true;  // the hotspot itself is protected by the PIN
  if (server.authenticate("neco", cfg.pin.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "neco-cyd", "user: neco, password: the PIN on the CYD (SET > MORE)");
  return false;
}

static void hRoot() {
  if (!authed()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", PORTAL_HTML);
}

static void hConfig() {
  if (!authed()) return;
  JsonDocument d;
  d["mode"] = setupMode ? "setup" : "admin";
  d["ip"] = WiFi.localIP().toString();
  JsonArray nets = d["nets"].to<JsonArray>();
  int n = WiFi.scanComplete();
  for (int i = 0; i < n && i < 20; i++) {
    if (!WiFi.SSID(i).length()) continue;
    JsonObject o = nets.add<JsonObject>(); o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i);
  }
  JsonArray saved = d["saved"].to<JsonArray>();
  for (int i = 0; i < savedCount(); i++) saved.add(savedSsid(i));
  d["model"] = cfg.model; d["name"] = cfg.userName; d["cat"] = cfg.cat; d["tz"] = cfg.tzHours; d["idle"] = cfg.idleMin;
  d["keyHint"] = cfg.apiKey.length() > 4 ? cfg.apiKey.substring(cfg.apiKey.length() - 4) : "";
  d["mem"] = memActiveCount();
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void hSave() {
  if (!authed()) return;
  String ssid = server.arg("ssid"), pass = server.arg("pass"), key = server.arg("key");
  ssid.trim();
  if (key.length()) cfg.apiKey = key;
  if (server.arg("model").length()) cfg.model = server.arg("model");
  if (server.arg("name").length()) cfg.userName = server.arg("name").substring(0, 24);
  cfg.cat = constrain(server.arg("cat").toInt(), 0, 2);
  if (server.arg("tz").length()) cfg.tzHours = constrain(server.arg("tz").toInt(), -12, 14);
  cfg.idleMin = constrain(server.arg("idle").toInt(), 0, 240);
  saveSettings();
  if (ssid.length()) saveNetwork(ssid, pass);
  if (!cfg.apiKey.length()) { server.send(200, "text/plain", "saved, but there's no api key yet. she can't think without one."); return; }
  if (!savedCount()) { server.send(200, "text/plain", "saved, but no wifi network yet."); return; }
  if (setupMode || ssid.length()) {
    server.send(200, "text/plain", "saved. restarting and joining " + (ssid.length() ? ssid : savedSsid(0)) +
                                       ". the web admin will be at http://neco-cyd.local");
    savedFlag = true;  // restart after the reply is flushed
  } else server.send(200, "text/plain", "saved.");
}

static void hMemory() {
  if (!authed()) return;
  if (server.hasArg("download")) server.sendHeader("Content-Disposition", "attachment; filename=neco-cyd-reflections.json");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  struct Out : Print {  // stream the export in chunks
    char buf[512]; size_t n = 0;
    size_t write(uint8_t c) override { buf[n++] = c; if (n == sizeof buf) flush(); return 1; }
    void flush() override { if (n) server.sendContent(buf, n); n = 0; }
  } out;
  memExport(out);
  out.flush();
  server.sendContent("");
}

static void hForget() {
  if (!authed()) return;
  server.send(memForget(server.arg("id").toInt()) ? 200 : 404, "text/plain", "ok");
}

static void hClear() {
  if (!authed()) return;
  memClear();
  server.send(200, "text/plain", "all reflections forgotten");
}

static void hImportUpload() {
  if (!setupMode && !server.authenticate("neco", cfg.pin.c_str())) return;
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) upload = LittleFS.open("/import.json", "w");
  else if (u.status == UPLOAD_FILE_WRITE && upload) upload.write(u.buf, u.currentSize);
  else if (u.status == UPLOAD_FILE_END && upload) upload.close();
}

static void hImportDone() {
  if (!authed()) return;
  String msg;
  bool ok = memImportFile("/import.json", msg);
  server.send(ok ? 200 : 400, "text/plain", msg);
}

static void hBodyGet() {
  if (!authed()) return;
  server.send(200, "text/plain; charset=utf-8", brainBodyNotes());
}

static void hBodySet() {
  if (!authed()) return;
  bool ok = brainSetBodyNotes(server.arg("text"));
  server.send(ok ? 200 : 500, "text/plain", ok ? "saved. she reads this on every message." : "couldn't write to flash");
}

static void hCaptive() {  // phones probe these to detect a captive portal
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

static void routes() {
  server.on("/", HTTP_GET, hRoot);
  server.on("/api/config", HTTP_GET, hConfig);
  server.on("/save", HTTP_POST, hSave);
  server.on("/api/memory", HTTP_GET, hMemory);
  server.on("/api/forget", HTTP_POST, hForget);
  server.on("/api/clear", HTTP_POST, hClear);
  server.on("/api/import", HTTP_POST, hImportDone, hImportUpload);
  server.on("/api/body", HTTP_GET, hBodyGet);
  server.on("/api/body", HTTP_POST, hBodySet);
  server.onNotFound([]() {
    if (setupMode) hCaptive();
    else server.send(404, "text/plain", "nothing here");
  });
}

// ---------------- setup hotspot ----------------
static void drawQr(const String &payload, int x, int y, int scale) {
  QRCode qr;
  uint8_t data[qrcode_getBufferSize(3)];
  qrcode_initText(&qr, data, 3, ECC_LOW, payload.c_str());  // v3 = 29x29, fits the ~41-char wifi string
  int size = qr.size * scale + 8;
  tft.fillRect(x, y, size, size, TFT_WHITE);  // quiet zone; QR needs light background
  for (int r = 0; r < qr.size; r++)
    for (int c = 0; c < qr.size; c++)
      if (qrcode_getModule(&qr, c, r)) tft.fillRect(x + 4 + c * scale, y + 4 + r * scale, scale, scale, TFT_BLACK);
}

void portalRun() {
  ensurePin();
  setupMode = true; savedFlag = false;
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  WiFi.scanNetworks(true);
  String ssid = apName();
  WiFi.softAP(ssid.c_str(), cfg.pin.c_str());
  delay(200);
  dns.start(53, "*", WiFi.softAPIP());
  if (!serverUp) { routes(); server.begin(); serverUp = true; }

  fxPause(true);
  clearScreen();
  statusBar("NECO//SETUP");
  drawQr("WIFI:T:WPA;S:" + ssid + ";P:" + cfg.pin + ";;", 6, 26, 4);
  int tx = 158;
  text(tx, 26, "1. scan or join", F_SMALL, C_DIM);
  text(tx, 44, ssid, F_SMALL, C_HOT);
  text(tx, 64, "pass " + cfg.pin, F_SMALL, C_AMBER);
  text(tx, 88, "2. the page opens", F_SMALL, C_DIM);
  text(tx, 104, "   by itself, or go", F_SMALL, C_DIM);
  text(tx, 120, "   to 192.168.4.1", F_SMALL, C_HOT);
  text(tx, 144, "3. wifi + deepseek", F_SMALL, C_DIM);
  text(tx, 160, "   key, then SAVE", F_SMALL, C_DIM);
  Btn skip{218, 202, 100, 36, "SKIP"};
  drawBtn(skip, false, C_RUST);
  text(8, 212, "skip = offline pet mode", F_SMALL, C_LINE);
  fxCorners();

  while (true) {
    dns.processNextRequest();
    server.handleClient();
    audioLoop();
    ledColor(0, (millis() / 400) % 2 ? 12 : 2, 6);
    if (savedFlag) {
      uint32_t t0 = millis();
      while (millis() - t0 < 800) { server.handleClient(); delay(5); }  // let the reply reach the phone
      clearScreen();
      text(160, 110, "saved. restarting...", F_BIG, C_LIME, MC_DATUM);
      playSfx(SFX_CHIRP);
      t0 = millis(); while (millis() - t0 < 900) { audioLoop(); delay(5); }
      ESP.restart();
    }
    int16_t x, y;
    if (pollTouch(x, y) == T_TAP && skip.hit(x, y)) break;
    delay(2);
  }
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  setupMode = false;
  fxPause(false);
}

// ---------------- web admin on your network ----------------
void adminBegin() {
  static bool mdnsUp = false;
  if (WiFi.status() != WL_CONNECTED) return;
  ensurePin();
  if (!serverUp) { routes(); server.begin(); serverUp = true; }
  if (!mdnsUp && MDNS.begin("neco-cyd")) { MDNS.addService("http", "tcp", 80); mdnsUp = true; }
}

void adminLoop() { if (serverUp) server.handleClient(); }

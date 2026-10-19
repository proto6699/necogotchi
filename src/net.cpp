#include "net.h"
#include "hw.h"
#include <WiFi.h>
#include <time.h>

static uint32_t lastTry = 0;
static bool ntpStarted = false;

int savedCount() {
  prefs.begin("wifi", true);
  int n = prefs.getUChar("n", 0);
  prefs.end();
  return n;
}

String savedSsid(int i) {
  prefs.begin("wifi", true);
  String s = prefs.getString(("s" + String(i)).c_str(), "");
  prefs.end();
  return s;
}

bool isSaved(const String &ssid, String *pass) {
  prefs.begin("wifi", true);
  int n = prefs.getUChar("n", 0);
  bool found = false;
  for (int i = 0; i < n && !found; i++) {
    if (prefs.getString(("s" + String(i)).c_str(), "") == ssid) {
      found = true;
      if (pass) *pass = prefs.getString(("p" + String(i)).c_str(), "");
    }
  }
  prefs.end();
  return found;
}

void saveNetwork(const String &ssid, const String &pass) {
  prefs.begin("wifi", false);
  int n = prefs.getUChar("n", 0);
  int slot = -1;
  for (int i = 0; i < n; i++)
    if (prefs.getString(("s" + String(i)).c_str(), "") == ssid) slot = i;
  if (slot < 0) {
    if (n < MAX_SAVED_NETS) slot = n++;
    else {  // full: shift out the oldest
      for (int i = 1; i < MAX_SAVED_NETS; i++) {
        prefs.putString(("s" + String(i - 1)).c_str(), prefs.getString(("s" + String(i)).c_str(), ""));
        prefs.putString(("p" + String(i - 1)).c_str(), prefs.getString(("p" + String(i)).c_str(), ""));
      }
      slot = MAX_SAVED_NETS - 1;
    }
  }
  prefs.putString(("s" + String(slot)).c_str(), ssid);
  prefs.putString(("p" + String(slot)).c_str(), pass);
  prefs.putUChar("n", n);
  prefs.end();
}

void forgetAll() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  WiFi.disconnect();
}

bool netConnected() { return WiFi.status() == WL_CONNECTED; }

static void startNtp() {
  configTime(cfg.tzHours * 3600, 0, "pool.ntp.org", "time.google.com");
  ntpStarted = true;
}

bool connectNow(const String &ssid, const String &pass, uint32_t timeoutMs) {
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    audioLoop();
    if (WiFi.status() == WL_CONNECTED) { startNtp(); return true; }
    delay(20);
  }
  return false;
}

// Pick the strongest saved network that is currently visible.
static void tryAutoConnect() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  if (n < 0) { WiFi.scanNetworks(true); return; }
  int best = -1, bestRssi = -999; String bestPass;
  for (int i = 0; i < n; i++) {
    String pass;
    if (isSaved(WiFi.SSID(i), &pass) && WiFi.RSSI(i) > bestRssi) { best = i; bestRssi = WiFi.RSSI(i); }
  }
  if (best >= 0) {
    String ssid = WiFi.SSID(best);
    isSaved(ssid, &bestPass);
    WiFi.scanDelete();
    WiFi.begin(ssid.c_str(), bestPass.length() ? bestPass.c_str() : nullptr);
  } else WiFi.scanDelete();
}

void netBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("neco-cyd");
  WiFi.setAutoReconnect(true);
  if (savedCount()) WiFi.scanNetworks(true);
  lastTry = millis();
}

void netTick() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!ntpStarted) startNtp();
    return;
  }
  if (!savedCount()) return;
  if (WiFi.scanComplete() >= 0) { tryAutoConnect(); lastTry = millis(); return; }
  if (millis() - lastTry > 30000) { lastTry = millis(); WiFi.scanNetworks(true); }
}


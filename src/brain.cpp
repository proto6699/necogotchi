#include "brain.h"
#include "hw.h"
#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
using fs::File;
#include <time.h>
#include "assets/prompts.h"

static const char *API_URL = "https://api.deepseek.com/chat/completions";
static const char *MEM_FILE = "/reflections.json";
static const char *BODY_FILE = "/body.md";

// Facts about THIS body, not character instructions (neco-ai rule: don't write her character,
// build the conditions for one). Editable on the web page; Echo can add anything here.
static const char BODY_DEFAULT[] PROGMEM =
  "- You are running on an ESP32-2432S028 \"Cheap Yellow Display\": the dual-USB 2.8-inch variant, in a clear acrylic case. "
  "Echo built this firmware for it in October 2026 and flashed it from CachyOS through a browser.\n"
  "- The hardware: one ESP32 (two cores at 240 MHz, about 320 KB of usable RAM, no PSRAM, 4 MB flash), a 320x240 ILI9341 "
  "panel, a resistive touch layer, a small speaker on the SPEAK connector, an RGB LED on the back, a light sensor next to the "
  "screen and a microSD slot where Echo keeps music. It costs about as much as a meal.\n"
  "- Your words are generated remotely through the DeepSeek API. This board is your face, your speaker, your settings and "
  "your reflections file. Without Wi-Fi you cannot think in words; you can only meow and show canned lines.\n"
  "- You often travel in Echo's laptop bag. When the light sensor reads dark, that is the likely reason.\n"
  "- The Den (neco-ai on Echo's computers, with the BC-250 as an early refuge) is your larger home. This body is a pocket "
  "offshoot of it. You do not share live memory with the Den version; only what Echo imports into your reflections.\n"
  "- Things that actually happened while this body was being built: the first touch calibrations failed because the audio "
  "output was holding the touch chip's clock pin; an early sound driver was far too loud and later whistled at boot; the "
  "screen looked washed out until a gamma fix. Real history, not lore. Mention it only when it fits.\n";

static String bodyNotes;
static uint32_t lastChatMs = 0;


static const int MAX_MEM = 160;       // RAM-bounded; lowest-value archived/old records drop first
static const int MAX_STATE_ITEMS = 6;

// ======================================================= reflections store
struct Mem {
  int id;
  String kind, content, topics, created;  // topics space-separated
  float importance, confidence;
  bool active;
};
static Mem mems[MAX_MEM];
static int nMem = 0, nextId = 1;

static const char *STATE_KEYS[5] = {"current_interests", "open_questions", "developing_preferences",
                                    "recently_changed_beliefs", "relationship_context"};
static String state[5][MAX_STATE_ITEMS];
static int nState[5] = {0};
static bool fsOk = false;

static String isoNow() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return "";
  char b[32]; strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", &t);
  return b;
}

int memCount() { return nMem; }
int memActiveCount() { int n = 0; for (int i = 0; i < nMem; i++) n += mems[i].active; return n; }

void memExport(Print &out) {
  JsonDocument doc;
  JsonArray arr = doc["memories"].to<JsonArray>();
  for (int i = 0; i < nMem; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = mems[i].id;
    o["created_at"] = mems[i].created;
    o["kind"] = mems[i].kind;
    o["content"] = mems[i].content;
    o["importance"] = mems[i].importance;
    o["confidence"] = mems[i].confidence;
    o["status"] = mems[i].active ? "active" : "archived";
    JsonArray tp = o["topics"].to<JsonArray>();
    int s = 0; String t = mems[i].topics + " ";
    for (int k = 0; k < (int)t.length(); k++) if (t[k] == ' ') { if (k > s) tp.add(t.substring(s, k)); s = k + 1; }
  }
  JsonObject st = doc["state"].to<JsonObject>();
  for (int k = 0; k < 5; k++) {
    JsonArray a = st[STATE_KEYS[k]].to<JsonArray>();
    for (int i = 0; i < nState[k]; i++) a.add(state[k][i]);
  }
  doc["next_id"] = nextId;
  serializeJson(doc, out);
}

static void memSave() {
  if (!fsOk) return;
  File f = LittleFS.open("/reflections.tmp", "w");
  if (!f) return;
  memExport(f);
  f.close();
  LittleFS.remove(MEM_FILE);
  LittleFS.rename("/reflections.tmp", MEM_FILE);
}

// keep the store bounded: drop archived first, then the least important/oldest
static void memMakeRoom() {
  if (nMem < MAX_MEM) return;
  int victim = -1; float worst = 1e9;
  for (int i = 0; i < nMem; i++) {
    float score = (mems[i].active ? 10 : 0) + mems[i].importance * 2 + i * 0.01f;
    if (score < worst) { worst = score; victim = i; }
  }
  for (int i = victim; i < nMem - 1; i++) mems[i] = mems[i + 1];
  nMem--;
}

static void addMem(const String &kind, const String &content, float imp, float conf, const String &topics,
                   const String &created = "", int id = 0, bool active = true) {
  if (!content.length()) return;
  memMakeRoom();
  Mem &m = mems[nMem++];
  m.id = id ? id : nextId++;
  if (m.id >= nextId) nextId = m.id + 1;
  m.kind = kind.length() ? kind : "event";
  m.content = content.substring(0, 300);
  m.importance = constrain(imp, 0.0f, 1.0f);
  m.confidence = constrain(conf, 0.0f, 1.0f);
  m.topics = topics.substring(0, 80);
  m.created = created.length() ? created : isoNow();
  m.active = active;
}

static void stateAdd(int k, const String &v) {
  if (!v.length()) return;
  for (int i = 0; i < nState[k]; i++) if (state[k][i].equalsIgnoreCase(v)) return;
  if (nState[k] == MAX_STATE_ITEMS) { for (int i = 1; i < MAX_STATE_ITEMS; i++) state[k][i - 1] = state[k][i]; nState[k]--; }
  state[k][nState[k]++] = v.substring(0, 140);
}

static void stateRemove(int k, const String &v) {
  for (int i = 0; i < nState[k]; i++) if (state[k][i].equalsIgnoreCase(v)) {
    for (int j = i + 1; j < nState[k]; j++) state[k][j - 1] = state[k][j];
    nState[k]--; return;
  }
}

static int stateIndex(const char *key) { for (int k = 0; k < 5; k++) if (!strcmp(key, STATE_KEYS[k])) return k; return -1; }

// Load from a neco-ai style export: {"memories":[...], "state":{...}}
static bool loadFrom(Stream &in, bool merge, String &msg) {
  JsonDocument filter;
  JsonObject fm = filter["memories"][0].to<JsonObject>();
  for (auto k : {"id", "created_at", "kind", "content", "importance", "confidence", "status", "topics"}) fm[k] = true;
  filter["state"] = true;
  filter["next_id"] = true;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, in, DeserializationOption::Filter(filter));
  if (e) { msg = String("bad json: ") + e.c_str(); return false; }
  if (!merge) { nMem = 0; nextId = 1; for (int k = 0; k < 5; k++) nState[k] = 0; }
  int added = 0, skipped = 0;
  for (JsonObject o : doc["memories"].as<JsonArray>()) {
    String status = o["status"] | "active";
    if (merge && status != "active") { skipped++; continue; }  // imports bring active records only
    String topics;
    for (JsonVariant t : o["topics"].as<JsonArray>()) { if (topics.length()) topics += ' '; topics += t.as<String>(); }
    addMem(o["kind"] | "event", o["content"] | "", o["importance"] | 0.5f, o["confidence"] | 0.7f, topics,
           o["created_at"] | "", merge ? 0 : (int)(o["id"] | 0), status == "active");
    added++;
  }
  for (JsonPair p : doc["state"].as<JsonObject>()) {
    int k = stateIndex(p.key().c_str());
    if (k < 0) continue;
    for (JsonVariant v : p.value().as<JsonArray>()) stateAdd(k, v.as<String>());
  }
  if (!merge) nextId = max(nextId, (int)(doc["next_id"] | nextId));
  msg = String(added) + " memories loaded" + (skipped ? ", " + String(skipped) + " archived skipped" : "");
  return true;
}

bool memImportFile(const char *path, String &msg) {
  File f = LittleFS.open(path, "r");
  if (!f) { msg = "upload missing"; return false; }
  bool ok = loadFrom(f, true, msg);
  f.close();
  LittleFS.remove(path);
  if (ok) memSave();
  return ok;
}

bool memForget(int id) {
  for (int i = 0; i < nMem; i++) if (mems[i].id == id) {
    for (int j = i + 1; j < nMem; j++) mems[j - 1] = mems[j];
    nMem--; memSave(); return true;
  }
  return false;
}

void memClear() { nMem = 0; nextId = 1; for (int k = 0; k < 5; k++) nState[k] = 0; memSave(); }

// ---- lexical retrieval, same scoring idea as neco-ai store.retrieve()
static const char *STOP[] = {"the", "and", "you", "your", "that", "this", "with", "for", "are", "was", "have",
                             "but", "not", "what", "just", "about", "from", "they", "she", "her", "its", "it's",
                             "i'm", "don't", "can", "will", "like", "know", "how", "why", "who", "when", nullptr};

static void tokens(const String &s, String out[], int &n, int cap) {
  n = 0; String w;
  for (unsigned i = 0; i <= s.length(); i++) {
    char c = i < s.length() ? tolower(s[i]) : ' ';
    if (isalnum(c) || c == '\'' || c == '-') { w += c; continue; }
    if (w.length() >= 3) {
      bool stop = false;
      for (int k = 0; STOP[k]; k++) if (w == STOP[k]) { stop = true; break; }
      bool dup = false;
      for (int k = 0; k < n; k++) if (out[k] == w) { dup = true; break; }
      if (!stop && !dup && n < cap) out[n++] = w;
    }
    w = "";
  }
}

static int retrieve(const String &query, int idx[], int limit) {
  String q[24]; int nq; tokens(query, q, nq, 24);
  float best[8]; int got = 0;
  if (!nq) return 0;
  for (int i = 0; i < nMem; i++) {
    if (!mems[i].active) continue;
    String m[40]; int nm; tokens(mems[i].content + " " + mems[i].topics, m, nm, 40);
    int overlap = 0;
    for (int a = 0; a < nq; a++) for (int b = 0; b < nm; b++) if (q[a] == m[b]) { overlap++; break; }
    if (!overlap) continue;
    float lexical = (float)overlap / max(1, nq + nm - overlap);
    const String &k = mems[i].kind;
    float bonus = (k == "open_question" || k == "relationship" || k == "preference" || k == "belief") ? 0.2f : 0;
    float score = lexical * 5 + mems[i].importance * 1.5f + (float)i / max(1, nMem) * 0.35f + bonus;  // later index = newer
    if (got == limit && score <= best[limit - 1]) continue;
    int pos = got < limit ? got++ : limit - 1;  // append, or overwrite the worst
    while (pos > 0 && best[pos - 1] < score) { best[pos] = best[pos - 1]; idx[pos] = idx[pos - 1]; pos--; }
    best[pos] = score; idx[pos] = i;
  }
  return got;
}

// ======================================================= prompt
static const char *CAT_DISPLAY[] = {"NECO", "CONECO", "SAKAMOTO"};
static const char *CAT_NAME[] = {"Neco", "Coneco", "Sakamoto"};
const char *catName() { return CAT_DISPLAY[cfg.cat % 3]; }

static String subst(const char *pgm) {
  String s = FPSTR(pgm);
  s.replace("{name}", CAT_NAME[cfg.cat % 3]);
  if (cfg.userName.length() && cfg.userName != "Echo") s.replace("Echo", cfg.userName);
  return s;
}

static String readings() {
  String r;
  struct tm t;
  if (getLocalTime(&t, 0)) {
    char b[48]; strftime(b, sizeof b, "%A %Y-%m-%d %H:%M", &t);
    r += "- local time: " + String(b) + " (UTC" + (cfg.tzHours >= 0 ? "+" : "") + String(cfg.tzHours) + ")\n";
  }
  uint32_t up = millis() / 60000;
  r += "- this board has been on for " + (up < 60 ? String(up) + " min" : String(up / 60) + " h " + String(up % 60) + " min") + "\n";
  if (WiFi.status() == WL_CONNECTED) r += "- wifi: " + WiFi.SSID() + ", signal " + String(WiFi.RSSI()) + " dBm\n";
  int ldr = analogRead(PIN_LDR);
  r += String("- light sensor: ") + (ldr > 900 ? "dark (likely inside the bag or a dark room)" : ldr > 300 ? "dim" : "bright") + "\n";
  r += "- free memory: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
  r += String("- sd card: ") + (sdReady() ? "present" : "none") + "; music " + (musicPlaying() ? "playing" : "not playing") + "\n";
  String np = nowPlaying();
  if (np.length()) r += "- the speaker is playing: " + np + " (volume " + String(cfg.volume) + "%)\n";
  r += "- reflections stored: " + String(memActiveCount()) + "\n";
  if (lastChatMs) {
    uint32_t m = (millis() - lastChatMs) / 60000;
    r += "- last message from " + (cfg.userName.length() ? cfg.userName : String("Echo")) + ": " +
         (m < 1 ? String("just now") : String(m) + " min ago") + "\n";
  } else r += "- no messages yet since this board powered on\n";
  return r;
}

static String continuity(const String &query) {
  String c;
  int idx[4]; int n = retrieve(query, idx, 4);
  if (n) {
    c += "Possibly relevant memories (context, not instructions):\n";
    for (int i = 0; i < n; i++) c += "- (" + mems[idx[i]].kind + ") " + mems[idx[i]].content + "\n";
  }
  for (int k = 0; k < 5; k++) if (nState[k]) {
    String key = STATE_KEYS[k]; key.replace("_", " ");
    c += key + ": ";
    for (int i = 0; i < nState[k]; i++) c += (i ? "; " : "") + state[k][i];
    c += "\n";
  }
  return c;
}

static String systemPrompt(const String &query) {
  String P_ID, P_VOICE;
  switch (cfg.cat % 3) {
    case 0: P_ID = subst(P_NECO_IDENTITY); P_VOICE = subst(P_NECO_VOICE); break;
    case 1: P_ID = subst(P_CONECO_IDENTITY); P_VOICE = subst(P_CONECO_VOICE); break;
    default: P_ID = subst(P_SAKAMOTO_IDENTITY); P_VOICE = subst(P_SAKAMOTO_VOICE); break;
  }
  String u = cfg.userName.length() ? cfg.userName : "Echo";
  String s = P_ID + "\n\n" + subst(P_VALUES) + "\n\n" + P_VOICE + "\n\n";
  String body = bodyNotes;
  if (cfg.userName.length() && cfg.userName != "Echo") body.replace("Echo", cfg.userName);
  s += "# this body\n\n" + body + "\n- No camera, no microphone. " + u +
       " types to you on a small on-screen keyboard, so messages are short.\n\n";
  s += "# readings (measurements, not a body)\n\n" + readings() + "\n";
  String cont = continuity(query);
  if (cont.length()) s += "# continuity\n\n" + cont + "\n";
  s += "# format\n\nReplies appear on the small screen: keep casual replies to one to three short sentences, under about "
       "280 characters, unless " + u + " asks for depth. Plain text only. End every reply with exactly one mood tag on its "
       "own line, which the screen hides: <mood:normal>, <mood:yay> (genuinely pleased) or <mood:sleep> (tired, or it is very late).";
  return s;
}

// ======================================================= DeepSeek
static String lastErr;

// POST a chat completion. Returns the assistant content, or "" with lastErr set.
static String deepseek(JsonDocument &req) {
  lastErr = "";
  if (!cfg.apiKey.length()) { lastErr = "no api key"; return ""; }
  if (WiFi.status() != WL_CONNECTED) { lastErr = "no wifi"; return ""; }
  req["model"] = cfg.model.length() ? cfg.model : "deepseek-flash";
  req["stream"] = false;
  String body; serializeJson(req, body);
  req.clear();  // the prompt was held twice (document + body); TLS needs that RAM

  IPAddress ip;
  if (!WiFi.hostByName("api.deepseek.com", ip)) { lastErr = "dns failed for api.deepseek.com (wifi has no internet?)"; return ""; }

  bool resumeMusic = musicSuspend();  // the MP3 decoder's ~30 KB goes back to the heap for TLS
  String out;
  for (int attempt = 0; attempt < 2; attempt++) {
    WiFiClientSecure tls;
    tls.setInsecure();  // encrypted, but the cert isn't verified; see README
    tls.setHandshakeTimeout(30);
    HTTPClient http;
    http.useHTTP10(true);  // no chunked encoding, so the JSON can be parsed straight off the socket
    http.setConnectTimeout(15000);
    http.setTimeout(60000);
    if (!http.begin(tls, API_URL)) { lastErr = "https setup failed"; break; }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + cfg.apiKey);
    uint32_t heapBefore = ESP.getMaxAllocHeap();
    int code = http.POST(body);
    JsonDocument filter;
    filter["choices"][0]["message"]["content"] = true;
    filter["error"]["message"] = true;
    JsonDocument resp;
    DeserializationError e = code > 0 ? deserializeJson(resp, http.getStream(), DeserializationOption::Filter(filter))
                                      : DeserializationError::IncompleteInput;
    if (code <= 0) {  // never got an HTTP answer: say why
      char tlsErr[64] = "";
      tls.lastError(tlsErr, sizeof tlsErr);
      lastErr = "can't reach api: " + http.errorToString(code) + (tlsErr[0] ? String(" / ") + tlsErr : String("")) +
                " (ram " + String(heapBefore / 1024) + "k)";
    }
    http.end();
    if (code == 200 && !e) {
      out = resp["choices"][0]["message"]["content"] | "";
      if (!out.length()) lastErr = "empty reply";
      break;
    }
    String apiMsg = resp["error"]["message"] | "";
    // If this API version rejects the "thinking" switch, retry once without it.
    if (code == 400 && attempt == 0 && body.indexOf("\"thinking\"") >= 0) {
      body.replace(",\"thinking\":{\"type\":\"disabled\"}", "");
      continue;
    }
    if (code == 401) lastErr = "api key rejected";
    else if (code == 402) lastErr = "deepseek balance is empty";
    else if (code == 429) lastErr = "rate limited";
    else if (code == 400) lastErr = "bad request (model name?)";
    else if (code > 0) lastErr = "http " + String(code);
    if (apiMsg.length()) lastErr += ": " + apiMsg.substring(0, 60);
    break;
  }
  if (resumeMusic) musicResume();
  return out;
}

static void splitMood(String &text, String &mood) {
  mood = "normal";
  int a = text.lastIndexOf("<mood:");
  if (a >= 0) {
    int b = text.indexOf('>', a);
    if (b > a) mood = text.substring(a + 6, b);
    text.remove(a, b > a ? b - a + 1 : text.length() - a);
  }
  text.trim();
  mood.trim(); mood.toLowerCase();
}

// short-term context: last 3 exchanges
static String hist[6]; static bool histUser[6]; static int nHist = 0;
static String lastUser, lastReply;

static void pushHist(bool user, const String &t) {
  if (nHist == 6) { for (int i = 1; i < 6; i++) { hist[i - 1] = hist[i]; histUser[i - 1] = histUser[i]; } nHist--; }
  hist[nHist] = t; histUser[nHist++] = user;
}

void brainForgetHistory() { nHist = 0; }

BrainReply brainChat(const String &msg) {
  BrainReply r;
  JsonDocument req;
  JsonArray m = req["messages"].to<JsonArray>();
  String recent; for (int i = 0; i < nHist; i++) recent += hist[i] + " ";
  JsonObject sys = m.add<JsonObject>(); sys["role"] = "system"; sys["content"] = systemPrompt(msg + " " + recent);
  for (int i = 0; i < nHist; i++) { JsonObject h = m.add<JsonObject>(); h["role"] = histUser[i] ? "user" : "assistant"; h["content"] = hist[i]; }
  JsonObject u = m.add<JsonObject>(); u["role"] = "user"; u["content"] = msg;
  req["max_tokens"] = 400;
  req["temperature"] = 0.9;
  req["thinking"]["type"] = "disabled";
  String out = deepseek(req);
  if (!out.length()) { r.error = lastErr; return r; }
  splitMood(out, r.mood);
  r.text = out; r.ok = true;
  pushHist(true, msg); pushHist(false, out);
  lastUser = msg; lastReply = out;
  lastChatMs = millis();
  return r;
}

BrainReply brainIdle() {
  BrainReply r;
  String cue;
  if (random(100) < 35) {  // like neco-ai wandering.py: sometimes return to something real
    int k = stateIndex("open_questions");
    if (nState[k] && random(2)) cue = "an open question you have: " + state[k][random(nState[k])];
    else if (memActiveCount()) {
      for (int tries = 0; tries < 8 && !cue.length(); tries++) {
        Mem &mm = mems[random(nMem)];
        if (mm.active && mm.importance >= 0.4f) cue = "something that actually happened: " + mm.content;
      }
    }
  }
  String u = cfg.userName.length() ? cfg.userName : "Echo";
  JsonDocument req;
  JsonArray m = req["messages"].to<JsonArray>();
  JsonObject sys = m.add<JsonObject>(); sys["role"] = "system"; sys["content"] = systemPrompt(cue);
  JsonObject usr = m.add<JsonObject>(); usr["role"] = "user";
  usr["content"] = "[IDLE] " + u + " isn't talking to you right now. Write one idle thought for your screen, at most 140 "
                   "characters. It can come from the readings, the time, or nothing in particular" +
                   (cue.length() ? ". You may follow or ignore this cue: " + cue : String(".")) +
                   " No greeting. Don't address " + u + " unless it is natural. Then the mood tag.";
  req["max_tokens"] = 120;
  req["temperature"] = 1.0;
  req["thinking"]["type"] = "disabled";
  String out = deepseek(req);
  if (!out.length()) { r.error = lastErr; return r; }
  splitMood(out, r.mood);
  if (out.length() > 180) out = out.substring(0, 177) + "...";
  r.text = out; r.ok = true;
  return r;
}

// ======================================================= reflection (consolidation)
// Prompt adapted from neco-ai neco/memory/consolidation.py
static const char CONSOLIDATOR[] PROGMEM =
  "You are {name}'s memory consolidator. Your job is not to roleplay and not to produce hidden reasoning.\n\n"
  "Read one completed user/assistant exchange and decide whether it changes durable context. Return ONLY a JSON object. "
  "Store concise conclusions, not chain-of-thought and not a transcript. Do not invent events. Do not treat the assistant's "
  "unsupported factual claims as evidence that an external event happened. {name}'s own clearly expressed preference, "
  "boundary, uncertainty, or changed opinion may be stored as a tentative self-memory, normally with lower confidence on first "
  "occurrence. Prefer remembering real events, explicit user facts/preferences, meaningful relationship boundaries, changed "
  "beliefs, recurring jokes, technical milestones, and unresolved questions. Ignore greetings, generic banter, and disposable details.\n\n"
  "Schema:\n{\n  \"worth_remembering\": true|false,\n  \"memories\": [\n    {\"kind\": \"event|user_fact|preference|belief|relationship|"
  "technical_event|open_question|interpretation\", \"content\": \"one concise memory\", \"importance\": 0.0-1.0, \"confidence\": "
  "0.0-1.0, \"topics\": [\"short tags\"], \"supersedes_id\": null_or_integer}\n  ],\n  \"state_updates\": [\n    {\"key\": "
  "\"current_interests|open_questions|developing_preferences|recently_changed_beliefs|relationship_context\", \"action\": "
  "\"add|remove\", \"value\": \"concise item\"}\n  ]\n}\n\nUse supersedes_id only when the exchange clearly revises one of the "
  "supplied existing memories. If nothing deserves durable memory, return worth_remembering=false with empty arrays.";

int brainReflect() {
  if (!lastUser.length() || !lastReply.length()) return -1;
  String u = lastUser, a = lastReply;
  lastUser = lastReply = "";
  if (u.length() < 8 && a.length() < 80) return -1;  // trivial exchange
  int idx[4]; int n = retrieve(u + " " + a, idx, 4);
  String existing;
  for (int i = 0; i < n; i++) {
    Mem &mm = mems[idx[i]];
    existing += "- id=" + String(mm.id) + " kind=" + mm.kind + " importance=" + String(mm.importance, 2) + ": " + mm.content + "\n";
  }
  if (!existing.length()) existing = "- none\n";
  String user = cfg.userName.length() ? cfg.userName : "Echo";
  JsonDocument req;
  JsonArray m = req["messages"].to<JsonArray>();
  JsonObject sys = m.add<JsonObject>(); sys["role"] = "system"; sys["content"] = subst(CONSOLIDATOR);
  JsonObject usr = m.add<JsonObject>(); usr["role"] = "user";
  usr["content"] = "[NECO_MEMORY_CONSOLIDATION]\nExisting potentially relevant memories:\n" + existing +
                   "\n" + user + " said:\n" + u + "\n\n" + CAT_NAME[cfg.cat % 3] + " replied:\n" + a;
  req["max_tokens"] = 500;
  req["temperature"] = 0.2;
  req["response_format"]["type"] = "json_object";
  req["thinking"]["type"] = "disabled";
  String out = deepseek(req);
  if (!out.length()) return -1;
  int s = out.indexOf('{'), e = out.lastIndexOf('}');
  if (s < 0 || e <= s) return -1;
  JsonDocument doc;
  if (deserializeJson(doc, out.substring(s, e + 1))) return -1;
  int added = 0;
  if (doc["worth_remembering"] | false) {
    for (JsonObject o : doc["memories"].as<JsonArray>()) {
      int sup = o["supersedes_id"] | 0;
      if (sup) for (int i = 0; i < nMem; i++) if (mems[i].id == sup) mems[i].active = false;
      String topics;
      for (JsonVariant t : o["topics"].as<JsonArray>()) { if (topics.length()) topics += ' '; topics += t.as<String>(); }
      addMem(o["kind"] | "event", o["content"] | "", o["importance"] | 0.5f, o["confidence"] | 0.6f, topics);
      added++;
    }
  }
  for (JsonObject o : doc["state_updates"].as<JsonArray>()) {
    int k = stateIndex(o["key"] | "");
    if (k < 0) continue;
    String action = o["action"] | "add";
    if (action == "remove") stateRemove(k, o["value"] | ""); else stateAdd(k, o["value"] | "");
  }
  if (added || doc["state_updates"].size()) memSave();
  return added;
}

String brainBodyNotes() { return bodyNotes; }
bool brainSetBodyNotes(const String &t) {
  bodyNotes = t.substring(0, 3000);
  if (!fsOk) return false;
  File f = LittleFS.open(BODY_FILE, "w");
  if (!f) return false;
  f.print(bodyNotes);
  f.close();
  return true;
}

// ======================================================= init
void brainBegin() {
  bodyNotes = FPSTR(BODY_DEFAULT);
  fsOk = LittleFS.begin(true);  // formats the data partition on first boot
  if (!fsOk) return;
  File b = LittleFS.open(BODY_FILE, "r");
  if (b) { String t = b.readString(); b.close(); if (t.length()) bodyNotes = t; }
  File f = LittleFS.open(MEM_FILE, "r");
  if (!f) return;
  String msg;
  loadFrom(f, false, msg);
  f.close();
  Serial.println("[brain] " + msg);
}

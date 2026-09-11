/* =========================================================================
 *  Ultimate Romantic Couple Lamp - ESP32 firmware
 *
 *  Fully non-blocking: every subsystem is a millis() driven state machine
 *  ticked from loop(). delay() is never used at runtime.
 *
 *  Modules: LED engine | vibration heartbeat | DFPlayer audio | OLED manager
 *           | touch input | NTP + anniversary engine | Firebase sync | Web UI+OTA
 *
 *  Arduino IDE sketch. Keep Config.h / OTA.h / WebPage.h in this same folder;
 *  the IDE shows them as tabs. Board: "ESP32 Dev Module", partition scheme
 *  "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)" so OTA has two slots.
 *
 *  Note: no function here uses default arguments - the IDE auto-generates
 *  prototypes for .ino files and would clash with them. Overloads instead.
 * ========================================================================= */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DFRobotDFPlayerMini.h>
#include <time.h>

#include "Config.h"
#include "OTA.h"
#include "FirebaseSync.h"
#include "WebPage.h"
#include "PortalPage.h"

/* =======================================================================
 *  Types & global state
 * ==================================================================== */

enum Mood : uint8_t { MOOD_NORMAL = 0, MOOD_MISS, MOOD_SLEEP, MOOD_WORK, MOOD_HUG };

struct Settings {
    char     name[24];
    char     firebaseApiKey[64];                   // Firebase Web API key
    char     firebaseDatabaseUrl[96];               // e.g. https://xxxx-default-rtdb.firebaseio.com
    char     deviceSecret[DEVICE_SECRET_BYTES * 2 + 1]; // this lamp's own identity with Firebase
    char     tz[40];                               // POSIX TZ string
    char     pin[PIN_MAX_LEN + 1];                 // empty = Web UI open to the LAN
} cfg;

struct LampState {
    bool    power   = true;
    uint8_t r = 255, g = 77, b = 141;
    uint8_t bright  = LED_DEFAULT_BRIGHT;
    uint8_t volume  = DFP_DEFAULT_VOLUME;   // local to this lamp, never synced
    Mood    mood    = MOOD_NORMAL;
    String  note    = "";
} st;

Preferences        prefs;
Adafruit_NeoPixel  strip(LED_COUNT, PIN_LED_DATA, LED_TYPE);
Adafruit_SSD1306   oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
HardwareSerial     dfpSerial(2);
DFRobotDFPlayerMini dfp;
WebServer          web(80);

bool     oledOk = false, dfpOk = false, mdnsOk = false;
String   datesJson = "[]";                 // serialized special-dates array
uint32_t bootMillis = 0;
uint32_t rebootAt   = 0;                   // 0 = no reboot scheduled

/* identity + pairing */
char     devMac[18]    = "";               // "AA:BB:CC:DD:EE:FF", read once at boot - for display
char     devMacKey[13] = "";               // "AABBCCDDEEFF" - the same MAC with the colons
                                            // stripped, since that's the Realtime Database key
                                            // shape (see firebase/database.rules.json) and the
                                            // shape the web app's own MAC input normalises to
bool     freshSecret    = false;           // minted this boot - needs saving
uint32_t lastPartnerMs  = 0;                // last update actually heard from them

/* Presence has no broker to notice a dead connection and flip a flag, so
 * staleness of the last lamp_sync update we heard from the partner is the
 * only signal there is - see FIREBASE_HEARTBEAT_MS / PARTNER_STALE_MS. */
static bool partnerIsOnline() {
    return Fb::isPaired() && lastPartnerMs && (millis() - lastPartnerMs) < PARTNER_STALE_MS;
}

/* anniversary alert */
bool     alertActive = false;
uint32_t alertStart  = 0;
String   alertName   = "";
int      lastCheckYday = -1;

/* pending OTA (executed from loop so the HTTP response can flush first) */
bool     otaPending = false;
String   otaUrl     = "";
String   otaLatest  = "";

/* =======================================================================
 *  Mood helpers
 * ==================================================================== */

static const char *moodKey(Mood m) {
    switch (m) {
        case MOOD_MISS:  return "miss";
        case MOOD_SLEEP: return "sleep";
        case MOOD_WORK:  return "work";
        case MOOD_HUG:   return "hug";
        default:         return "normal";
    }
}

static Mood moodFromKey(const String &k) {
    if (k == "miss")  return MOOD_MISS;
    if (k == "sleep") return MOOD_SLEEP;
    if (k == "work")  return MOOD_WORK;
    if (k == "hug")   return MOOD_HUG;
    return MOOD_NORMAL;
}

static const char *moodText(Mood m) {
    switch (m) {
        case MOOD_MISS:  return "MISSING YOU";
        case MOOD_SLEEP: return "SLEEPING";
        case MOOD_WORK:  return "WORKING";
        case MOOD_HUG:   return "NEED A HUG";
        default:         return "TOGETHER";
    }
}

/* =======================================================================
 *  Settings persistence (NVS)
 * ==================================================================== */

/* Mints this lamp's identity with Firebase: DEVICE_SECRET_BYTES of randomness
 * from esp_random() (mixed with the eFuse MAC so two lamps booting in the
 * same microsecond still diverge), hex-encoded. Used as the password for a
 * Firebase Auth account under a MAC-derived email - see FirebaseSync.h and
 * firebase/database.rules.json for how Realtime Database rules use the
 * resulting stable auth.uid to scope a request to exactly this one lamp. */
static void mintDeviceSecret(char *out, size_t cap) {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t mix = esp_random() ^ (uint32_t)mac ^ (uint32_t)(mac >> 32) ^ micros();

    size_t n = 0;
    for (int i = 0; i < DEVICE_SECRET_BYTES && n + 2 < cap; i++) {
        mix = mix * 1664525UL + 1013904223UL + esp_random();
        uint8_t byte = (uint8_t)(mix >> 13);
        n += snprintf(out + n, cap - n, "%02x", byte);
    }
    out[n] = '\0';
}

/* "AA:BB:CC:DD:EE:FF" -> "AABBCCDDEEFF" - the Realtime Database key shape
 * (see devMacKey's own comment above), computed once devMac is known. */
static void computeMacKey() {
    size_t n = 0;
    for (const char *p = devMac; *p && n + 1 < sizeof(devMacKey); p++) {
        if (isxdigit((unsigned char)*p)) devMacKey[n++] = (char)toupper((unsigned char)*p);
    }
    devMacKey[n] = '\0';
}

static void settingsLoad() {
    prefs.begin(PREF_NAMESPACE, true);
    strlcpy(cfg.name,     prefs.getString("name",  DEF_DEVICE_NAME).c_str(), sizeof(cfg.name));
    strlcpy(cfg.firebaseApiKey,      prefs.getString("fbkey", DEF_FIREBASE_API_KEY).c_str(),      sizeof(cfg.firebaseApiKey));
    strlcpy(cfg.firebaseDatabaseUrl, prefs.getString("fburl", DEF_FIREBASE_DATABASE_URL).c_str(), sizeof(cfg.firebaseDatabaseUrl));
    String storedSecret = prefs.getString("dsec", "");
    strlcpy(cfg.tz,       prefs.getString("tz", DEF_TZ).c_str(),             sizeof(cfg.tz));
    strlcpy(cfg.pin,      prefs.getString("pin", "").c_str(),                sizeof(cfg.pin));

    datesJson  = prefs.getString("dates", "[]");
    st.volume  = prefs.getUChar("vol", DFP_DEFAULT_VOLUME);
    st.power   = prefs.getBool("power", true);
    st.r       = prefs.getUChar("r", 255);
    st.g       = prefs.getUChar("g", 77);
    st.b       = prefs.getUChar("b", 141);
    st.bright  = prefs.getUChar("bright", LED_DEFAULT_BRIGHT);
    st.mood    = (Mood)prefs.getUChar("mood", MOOD_NORMAL);
    st.note    = prefs.getString("note", "");
    prefs.end();

    if (storedSecret.isEmpty()) {            // first boot, or after a factory wipe
        mintDeviceSecret(cfg.deviceSecret, sizeof(cfg.deviceSecret));
        freshSecret = true;
        Serial.println(F("[FB] minted a new device secret"));
    } else {
        strlcpy(cfg.deviceSecret, storedSecret.c_str(), sizeof(cfg.deviceSecret));
    }
}

static void settingsSave() {
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString("name",  cfg.name);
    prefs.putString("fbkey", cfg.firebaseApiKey);
    prefs.putString("fburl", cfg.firebaseDatabaseUrl);
    prefs.putString("dsec",  cfg.deviceSecret);
    prefs.putString("tz",    cfg.tz);
    prefs.putString("pin",   cfg.pin);
    prefs.end();
}

/* The lamp state is written back lazily: a dirty flag is set on every change
 * and flushed at most once every few seconds so NVS never sees a write storm
 * while a colour picker is being dragged. */
bool     stateDirty = false;
uint32_t stateDirtyAt = 0;

static void markStateDirty() { stateDirty = true; stateDirtyAt = millis(); }

static void stateFlushTask() {
    if (!stateDirty || millis() - stateDirtyAt < 3000) return;
    stateDirty = false;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putBool ("power",  st.power);
    prefs.putUChar("r",      st.r);
    prefs.putUChar("g",      st.g);
    prefs.putUChar("b",      st.b);
    prefs.putUChar("bright", st.bright);
    prefs.putUChar("vol",    st.volume);
    prefs.putUChar("mood",   (uint8_t)st.mood);
    prefs.putString("note",  st.note);
    prefs.end();
}

static void datesSave(const String &json) {
    datesJson = json;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString("dates", datesJson);
    prefs.end();
    lastCheckYday = -1;                     // re-evaluate today against new list
}

/* =======================================================================
 *  Vibration engine (GPIO 13) - heartbeat double pulse
 *  100 ON -> 100 OFF -> 100 ON -> 600 OFF, repeated VIB_REPEATS times.
 * ==================================================================== */

static const uint16_t VIB_DUR[4] = { VIB_BEAT_ON_MS, VIB_BEAT_GAP_MS, VIB_BEAT_ON_MS, VIB_BEAT_REST_MS };
static const bool     VIB_ON [4] = { true,           false,           true,           false };

struct {
    bool     active = false;
    uint8_t  step   = 0;
    uint8_t  rep    = 0;
    uint32_t tNext  = 0;
} vib;

static void vibDrive(bool on) { analogWrite(PIN_VIBRATION, on ? VIB_POWER : 0); }

static void vibApplyStep() {
    vibDrive(VIB_ON[vib.step]);
    vib.tNext = millis() + VIB_DUR[vib.step];
}

static void vibStop() { vib.active = false; vibDrive(false); }

static void vibStart() {
    vib.active = true;
    vib.step = 0;
    vib.rep  = 0;
    vibApplyStep();
}

static void vibTask() {
    if (!vib.active) return;
    if ((int32_t)(millis() - vib.tNext) < 0) return;

    if (++vib.step >= 4) {
        vib.step = 0;
        if (++vib.rep >= VIB_REPEATS) { vibStop(); return; }
    }
    vibApplyStep();
}

/* =======================================================================
 *  Audio (DFPlayer Mini on Serial2)
 * ==================================================================== */

static void applyVolume() {
    if (dfpOk) dfp.volume(constrain((int)st.volume, 0, DFP_VOLUME_MAX));
}

static void audioBegin() {
    dfpSerial.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
    dfpOk = dfp.begin(dfpSerial, /*isACK=*/true, /*doReset=*/true);
    if (dfpOk) {
        applyVolume();
        Serial.println(F("[DFP] ready"));
    } else {
        Serial.println(F("[DFP] not detected - audio disabled"));
    }
}

static void playTrack(uint8_t track) {
    if (!dfpOk || track == 0) return;
    dfp.play(track);
}

/* =======================================================================
 *  LED engine (WS2812B) - all animations are phase functions of millis()
 * ==================================================================== */

/* Smooth 0..1 cosine wave over `periodMs`, remapped into [lo,hi]. */
static float wave(uint32_t periodMs, float lo, float hi) {
    float t = (float)(millis() % periodMs) / (float)periodMs;
    float s = 0.5f * (1.0f - cosf(2.0f * (float)PI * t));
    return lo + (hi - lo) * s;
}

/* Two quick thumps per cycle - the visual twin of the vibration heartbeat. */
static float heartEnvelope(uint32_t periodMs) {
    uint32_t t = millis() % periodMs;
    auto thump = [](uint32_t x, uint32_t w) -> float {
        return (x < w) ? 0.5f * (1.0f - cosf(2.0f * (float)PI * (float)x / (float)w)) : 0.0f;
    };
    float v = thump(t, 180) + ((t >= 260 && t < 440) ? thump(t - 260, 180) * 0.75f : 0.0f);
    return 0.18f + 0.82f * min(v, 1.0f);
}

static void fillScaled(uint8_t r, uint8_t g, uint8_t b, float k) {
    k = constrain(k, 0.0f, 1.0f);
    uint32_t c = strip.Color((uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k));
    for (uint16_t i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, c);
}

/* Anniversary look: rainbow sweep cross-faded with a pink heartbeat pulse. */
static void renderCelebration() {
    const uint8_t pr = 255, pg = 45, pb = 130;        // celebration pink
    uint16_t base = (uint16_t)((millis() * 12) & 0xFFFF);
    float    pink = wave(900, 0.35f, 1.0f);           // pulse envelope
    float    m    = wave(4000, 0.0f, 1.0f);           // slow rainbow <-> pink cross-fade
    for (uint16_t i = 0; i < LED_COUNT; i++) {
        uint16_t hue = base + (uint16_t)(i * (65536UL / LED_COUNT));
        uint32_t rc  = strip.gamma32(strip.ColorHSV(hue, 255, 255));
        uint8_t rr = (rc >> 16) & 0xFF, rg = (rc >> 8) & 0xFF, rb = rc & 0xFF;
        strip.setPixelColor(i, strip.Color(
            (uint8_t)((rr * (1 - m) + pr * m) * pink),
            (uint8_t)((rg * (1 - m) + pg * m) * pink),
            (uint8_t)((rb * (1 - m) + pb * m) * pink)));
    }
}

static void ledTask() {
    static uint32_t last = 0;
    if (millis() - last < LED_FRAME_MS) return;
    last = millis();

    strip.setBrightness(st.bright);

    if (alertActive) {
        renderCelebration();
    } else if (!st.power) {
        strip.clear();
    } else {
        switch (st.mood) {
            case MOOD_MISS:                                  // soft pink longing breath
                fillScaled(255, 60, 140, wave(3200, 0.15f, 1.0f));
                break;
            case MOOD_SLEEP:                                 // very slow dim warm amber
                fillScaled(255, 120, 30, wave(8000, 0.05f, 0.35f));
                break;
            case MOOD_WORK:                                  // steady calm blue
                fillScaled(30, 120, 255, 0.55f);
                break;
            case MOOD_HUG:                                   // urgent red heartbeat
                fillScaled(255, 40, 60, heartEnvelope(1200));
                break;
            default:                                         // user colour, gentle breath
                fillScaled(st.r, st.g, st.b, wave(6000, 0.72f, 1.0f));
                break;
        }
    }
    strip.show();
}

/* =======================================================================
 *  Firebase synchronisation
 * ==================================================================== */

bool applyingRemote = false;                 // suppresses publish echo

static void publishState(bool wantVibrate, uint8_t track, const char *alert) {
    if (applyingRemote) return;

    Fb::OutgoingState s;
    s.power = st.power;
    snprintf(s.color, sizeof(s.color), "#%02x%02x%02x", st.r, st.g, st.b);
    s.bright = st.bright;
    strlcpy(s.mood, moodKey(st.mood), sizeof(s.mood));
    strlcpy(s.note, st.note.c_str(), sizeof(s.note));
    s.vib   = wantVibrate;
    s.track = track;
    strlcpy(s.alert, alert ? alert : "", sizeof(s.alert));

    Fb::sendUpdate(s);
}

static void publishState(bool wantVibrate, uint8_t track) { publishState(wantVibrate, track, nullptr); }
static void publishState()                                { publishState(false, 0, nullptr); }

static bool parseHexColor(const String &hexIn, uint8_t &r, uint8_t &g, uint8_t &b) {
    String h = hexIn;
    h.trim();
    if (h.startsWith("#")) h.remove(0, 1);
    if (h.length() != 6) return false;
    long v = strtol(h.c_str(), nullptr, 16);
    r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF;
    return true;
}

static void triggerAlert(const String &name, bool propagate);

static void applyIncoming(const Fb::IncomingState &in) {
    lastPartnerMs = millis();
    applyingRemote = true;

    uint8_t r, g, b;
    if (parseHexColor(String(in.color), r, g, b)) { st.r = r; st.g = g; st.b = b; }
    st.power  = in.power;
    st.bright = constrain((int)in.bright, 5, 255);

    Mood m = moodFromKey(String(in.mood));
    if (m != st.mood) {
        st.mood = m;
        if (m == MOOD_SLEEP) playTrack(TRACK_LULLABY);
    }

    String n(in.note);
    if (n != st.note) {
        st.note = n.substring(0, NOTE_MAX_LEN);
        playTrack(TRACK_CHIME);
        vibStart();
    }

    if (in.vib)     vibStart();
    if (in.track)   playTrack(in.track);
    if (in.alert[0]) triggerAlert(String(in.alert), false);

    applyingRemote = false;
    markStateDirty();
}

static void firebaseTask() {
    if (WiFi.status() != WL_CONNECTED) return;

    static bool wasPaired = false;
    bool paired = Fb::isPaired();
    if (paired && !wasPaired) {
        Serial.printf("[FB] paired - role %c\n", Fb::role());
        lastPartnerMs = 0;                  // fresh pairing, nothing heard yet
    }
    wasPaired = paired;

    if (!paired) return;                    // nothing to poll/heartbeat until paired

    Fb::IncomingState in;
    if (Fb::pollIncoming(in)) applyIncoming(in);

    // With no state change to report, still touch our row periodically so it
    // doesn't go stale in the partner's eyes while we're perfectly online.
    static uint32_t lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= FIREBASE_HEARTBEAT_MS) {
        lastHeartbeat = millis();
        publishState();
    }
}

/* =======================================================================
 *  Anniversary engine
 * ==================================================================== */

static bool timeValid() { return time(nullptr) > 1700000000; }

static void triggerAlert(const String &name, bool propagate) {
    alertActive = true;
    alertStart  = millis();
    alertName   = name.length() ? name : String("Our special day");
    playTrack(TRACK_CELEBRATION);
    vibStart();
    Serial.printf("[ALERT] %s\n", alertName.c_str());
    if (propagate) publishState(false, 0, alertName.c_str());
}

static void anniversaryTask() {
    if (alertActive && millis() - alertStart > ALERT_DURATION_MS) alertActive = false;

    static uint32_t last = 0;
    if (millis() - last < 30000UL) return;            // a daily check needs no hurry
    last = millis();

    if (!timeValid()) return;
    struct tm tmNow;
    if (!getLocalTime(&tmNow, 0)) return;
    if (tmNow.tm_yday == lastCheckYday) return;
    lastCheckYday = tmNow.tm_yday;

    JsonDocument doc;
    if (deserializeJson(doc, datesJson)) return;

    // Two anniversaries can share a date - name them all rather than the first.
    String hits;
    for (JsonObject d : doc.as<JsonArray>()) {
        if ((int)(d["day"] | 0) == tmNow.tm_mday && (int)(d["month"] | 0) == tmNow.tm_mon + 1) {
            if (hits.length()) hits += " + ";
            hits += String((const char *)(d["name"] | "Our special day"));
        }
    }
    if (hits.length()) triggerAlert(hits, true);
}

/* Long-run canary: String churn on the 4 s poll path is the thing most likely
 * to fragment the heap. Watch the largest free block, not just the total. */
static void heapTask() {
    static uint32_t last = 0;
    if (millis() - last < HEAP_LOG_MS) return;
    last = millis();
    Serial.printf("[HEAP] free %u  largest block %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

/* =======================================================================
 *  OLED display manager
 * ==================================================================== */

enum Screen : uint8_t { SCR_TIME = 0, SCR_MOOD, SCR_NOTE, SCR_PAIR, SCR_COUNT };
uint8_t  screen = SCR_TIME;
uint32_t screenAt = 0;

/* Centres short text, scrolls it when it is wider than the panel. */
static void drawLine(int16_t y, const String &text, uint8_t size, bool allowScroll) {
    oled.setTextSize(size);
    int16_t w = (int16_t)text.length() * 6 * size;
    if (w <= OLED_WIDTH || !allowScroll) {
        oled.setCursor((OLED_WIDTH - w) / 2, y);
        oled.print(text);
    } else {
        int16_t span = w + 24;
        int16_t x = OLED_WIDTH - (int16_t)((millis() / 33) % (uint32_t)(span + OLED_WIDTH));
        oled.setCursor(x, y);
        oled.print(text);
    }
}

static void drawLine(int16_t y, const String &text, uint8_t size) { drawLine(y, text, size, true); }

static void drawHeart(int16_t x, int16_t y, bool big) {
    uint8_t r = big ? 4 : 3;
    oled.fillCircle(x - r + 1, y, r, SSD1306_WHITE);
    oled.fillCircle(x + r - 1, y, r, SSD1306_WHITE);
    oled.fillTriangle(x - 2 * r + 1, y + 1, x + 2 * r - 1, y + 1, x, y + 2 * r, SSD1306_WHITE);
}

/* Shown instead of the normal screen rotation whenever this lamp has no
 * pair_id yet - the one thing the user actually needs to see is how to fix
 * that, so it takes over the whole display rather than taking a turn in it. */
static void drawOnboarding() {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);

    bool authTrouble = Fb::hasAuthTrouble();
    bool claimed     = Fb::isClaimed();

    drawLine(2, F("PAIR THIS LAMP"), 1, false);
    oled.drawFastHLine(14, 14, 100, SSD1306_WHITE);

    if (authTrouble) {
        drawLine(24, F("can't sign in - check Wi-Fi,"), 1);
        drawLine(36, F("or this MAC is already used"), 1);
    } else if (!claimed) {
        drawLine(24, strlen(WEBAPP_HOST) ? F("register this MAC at") : F("register this MAC in"), 1);
        drawLine(36, strlen(WEBAPP_HOST) ? String(WEBAPP_HOST) : String("the Love Lamp app"), 1);
    } else {
        drawLine(24, F("registered - now create"), 1);
        drawLine(36, F("or join a pair in the app"), 1);
    }

    drawLine(50, devMac[0] ? devMac : "reading MAC...", 1);

    oled.drawPixel(0, 0, WiFi.status() == WL_CONNECTED ? SSD1306_WHITE : SSD1306_BLACK);
    oled.drawPixel(2, 0, Fb::healthy() ? SSD1306_WHITE : SSD1306_BLACK);
    oled.display();
}

static void displayTask() {
    if (!oledOk) return;

    static uint32_t last = 0;
    if (millis() - last < SCREEN_FRAME_MS) return;
    last = millis();

    if (!Fb::isPaired()) { drawOnboarding(); return; }

    if (!alertActive && millis() - screenAt > SCREEN_ROTATE_MS) {
        screenAt = millis();
        screen = (screen + 1) % SCR_COUNT;
        if (screen == SCR_NOTE && st.note.isEmpty()) screen = SCR_TIME;   // skip empty note
    }

    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);

    if (alertActive) {
        bool blink = (millis() / 400) % 2;
        if (blink) { drawHeart(20, 12, true); drawHeart(108, 12, true); }
        drawLine(6, F("HAPPY"), 2, false);
        drawLine(26, alertName, 1);
        drawLine(42, F("I LOVE YOU"), 1, false);
        drawLine(54, F("* * * * * * * * *"), 1, false);
    } else {
        struct tm tmNow;
        bool haveTime = timeValid() && getLocalTime(&tmNow, 0);
        char buf[32];

        switch (screen) {
            case SCR_TIME:
                if (haveTime) {
                    strftime(buf, sizeof(buf), "%H:%M", &tmNow);
                    drawLine(8, buf, 3, false);
                    strftime(buf, sizeof(buf), "%a %d %b %Y", &tmNow);
                    drawLine(40, buf, 1, false);
                } else {
                    drawLine(16, F("--:--"), 3, false);
                    drawLine(44, F("waiting for NTP"), 1, false);
                }
                { char roleBuf[2] = { Fb::role() ? Fb::role() : '?', 0 };
                  drawLine(54, String(cfg.name) + ".local  " + roleBuf, 1, false); }
                break;

            case SCR_MOOD:
                drawLine(4, F("MOOD"), 1, false);
                drawLine(18, moodText(st.mood), 2);
                drawHeart(64, 46, true);
                drawLine(56, st.power ? F("lamp on") : F("lamp off"), 1, false);
                break;

            case SCR_NOTE:
                drawLine(2, F("LOVE NOTE"), 1, false);
                drawLine(22, st.note, 1);
                drawLine(46, F("- from your other half -"), 1);
                break;

            case SCR_PAIR:
                drawLine(2, F("PAIRED"), 1, false);
                { char roleBuf[8]; snprintf(roleBuf, sizeof(roleBuf), "role %c", Fb::role() ? Fb::role() : '?');
                  drawLine(18, roleBuf, 2); }
                drawHeart(64, 46, true);
                drawLine(56, partnerIsOnline() ? F("partner is online") : F("waiting for partner"), 1, false);
                break;
        }

        // connectivity footer dots
        oled.drawPixel(0, 0, WiFi.status() == WL_CONNECTED ? SSD1306_WHITE : SSD1306_BLACK);
        oled.drawPixel(2, 0, Fb::healthy() ? SSD1306_WHITE : SSD1306_BLACK);
    }
    oled.display();
}

static void displaySplash(const __FlashStringHelper *l1, const String &l2) {
    if (!oledOk) return;
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    drawLine(10, String(l1), 1, false);
    drawLine(28, l2, 1, false);
    drawHeart(64, 52, true);
    oled.display();
}

/* =======================================================================
 *  Touch input (TTP223, active HIGH)
 *  short tap  -> buzz + chime on the partner's lamp
 *  long press -> toggle this lamp's power
 * ==================================================================== */

struct {
    bool     stable = false, raw = false, longFired = false;
    uint32_t tChange = 0, tPress = 0;
} touch;

static void touchTask() {
    bool now = digitalRead(PIN_TOUCH) == HIGH;

    if (now != touch.raw) { touch.raw = now; touch.tChange = millis(); }
    if (millis() - touch.tChange < TOUCH_DEBOUNCE_MS) return;
    if (touch.raw == touch.stable) {
        // held: fire the long-press action once, without waiting for release
        if (touch.stable && !touch.longFired && millis() - touch.tPress > TOUCH_LONGPRESS_MS) {
            touch.longFired = true;
            st.power = !st.power;
            markStateDirty();
            publishState();
            vibStart();
        }
        return;
    }

    touch.stable = touch.raw;
    if (touch.stable) {                                  // press
        touch.tPress = millis();
        touch.longFired = false;
    } else if (!touch.longFired) {                       // release = short tap
        playTrack(TRACK_CHIME);
        vibStart();
        publishState(true, TRACK_CHIME);
        Serial.println(F("[TOUCH] thinking of you"));
    }
}

/* =======================================================================
 *  Web server
 * ==================================================================== */

/* Returns true when the request was answered with a 401 and the caller must
 * bail out. With no PIN set the gate is simply open. */
static bool denied() {
    if (!cfg.pin[0]) return false;
    if (web.authenticate(WEB_AUTH_USER, cfg.pin)) return false;
    web.requestAuthentication(BASIC_AUTH, "Love Lamp", "Enter the lamp PIN");
    return true;
}

static String buildStateJson() {
    JsonDocument doc;
    doc["fw"]      = FW_VERSION;
    doc["name"]    = cfg.name;
    doc["mac"]     = devMac;
    doc["paired"]     = Fb::isPaired();
    doc["claimed"]    = Fb::isClaimed();
    doc["authTrouble"] = Fb::hasAuthTrouble();
    { char roleBuf[2] = { Fb::role() ? Fb::role() : '?', 0 }; doc["role"] = roleBuf; }
    doc["linked"] = partnerIsOnline();
    doc["wifi"]   = WiFi.status() == WL_CONNECTED;
    doc["ip"]     = WiFi.localIP().toString();
    doc["power"]  = st.power;
    doc["r"]      = st.r;
    doc["g"]      = st.g;
    doc["b"]      = st.b;
    doc["bright"] = st.bright;
    doc["vol"]    = st.volume;
    doc["audio"]  = dfpOk;
    doc["heap"]   = (uint32_t)ESP.getFreeHeap();
    doc["block"]  = (uint32_t)ESP.getMaxAllocHeap();
    doc["mood"]   = moodKey(st.mood);
    doc["note"]   = st.note;
    doc["alert"]  = alertActive ? alertName : "";
    doc["uptime"] = (millis() - bootMillis) / 1000;

    struct tm tmNow;
    char buf[32] = "";
    if (timeValid() && getLocalTime(&tmNow, 0)) strftime(buf, sizeof(buf), "%a %d %b %H:%M", &tmNow);
    doc["time"] = buf;

    String out;
    serializeJson(doc, out);
    return out;
}

static void sendState() {
    if (denied()) return;
    web.send(200, "application/json", buildStateJson());
}

static bool readBody(JsonDocument &doc) {
    if (!web.hasArg("plain")) return false;
    return deserializeJson(doc, web.arg("plain")) == DeserializationError::Ok;
}

static void handleRoot() {
    if (denied()) return;
    web.sendHeader("Cache-Control", "no-store");
    web.send_P(200, "text/html", INDEX_HTML);
}

static void handleControl() {
    if (denied()) return;
    JsonDocument d;
    if (!readBody(d)) { web.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }

    if (!d["power"].isNull())      st.power  = d["power"].as<bool>();
    if (!d["brightness"].isNull()) st.bright = constrain((int)d["brightness"].as<int>(), 5, 255);
    if (!d["volume"].isNull()) {   // this lamp's speaker only - never synced
        st.volume = constrain((int)d["volume"].as<int>(), 0, DFP_VOLUME_MAX);
        applyVolume();
    }
    if (!d["color"].isNull()) {
        uint8_t r, g, b;
        if (parseHexColor(String((const char *)(d["color"] | "")), r, g, b)) { st.r = r; st.g = g; st.b = b; }
    }
    markStateDirty();
    publishState();
    sendState();
}

static void handleMood() {
    if (denied()) return;
    JsonDocument d;
    if (!readBody(d)) { web.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }

    st.mood = moodFromKey(String((const char *)(d["mood"] | "normal")));
    if (st.mood == MOOD_SLEEP) playTrack(TRACK_LULLABY);
    markStateDirty();
    publishState();
    sendState();
}

static void handleNote() {
    if (denied()) return;
    JsonDocument d;
    if (!readBody(d)) { web.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }

    st.note = String((const char *)(d["note"] | "")).substring(0, NOTE_MAX_LEN);
    screen  = SCR_NOTE;
    screenAt = millis();
    markStateDirty();
    publishState(true, TRACK_CHIME);        // their lamp chimes and buzzes
    sendState();
}

static void handleSound() {
    if (denied()) return;
    JsonDocument d;
    if (!readBody(d)) { web.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }

    uint8_t track = constrain((int)(d["track"] | 1), 1, 255);
    String target = String((const char *)(d["target"] | "partner"));
    if (target == "local" || target == "both") playTrack(track);
    if (target != "local")                     publishState(false, track);
    web.send(200, "application/json", "{\"ok\":true}");
}

static void handleVibrate() {
    if (denied()) return;
    JsonDocument d;
    readBody(d);
    String target = String((const char *)(d["target"] | "partner"));
    if (target == "local" || target == "both") vibStart();
    if (target != "local")                     publishState(true, TRACK_CHIME);
    web.send(200, "application/json", "{\"ok\":true}");
}

static void handleDatesGet() {
    if (denied()) return;
    web.send(200, "application/json", String("{\"dates\":") + datesJson + "}");
}

static void handleDatesPost() {
    if (denied()) return;
    JsonDocument d;
    if (!readBody(d) || !d["dates"].is<JsonArray>()) {
        web.send(400, "application/json", "{\"error\":\"expected {dates:[...]}\"}");
        return;
    }

    JsonDocument out;
    JsonArray arr = out.to<JsonArray>();
    for (JsonObject item : d["dates"].as<JsonArray>()) {
        int day = item["day"] | 0, month = item["month"] | 0;
        String name = String((const char *)(item["name"] | ""));
        if (day < 1 || day > 31 || month < 1 || month > 12 || name.isEmpty()) continue;
        if ((int)arr.size() >= MAX_SPECIAL_DATES) break;
        JsonObject o = arr.add<JsonObject>();
        o["name"]  = name.substring(0, 20);
        o["day"]   = day;
        o["month"] = month;
    }

    String json;
    serializeJson(arr, json);
    datesSave(json);
    web.send(200, "application/json", String("{\"ok\":true,\"dates\":") + datesJson + "}");
}

/* Fires the full anniversary routine on this lamp so the speaker, motor and
 * strip can be verified on assembly day instead of on the actual date.
 * Deliberately not propagated - a rehearsal should not page your partner. */
static void handleTestAlert() {
    if (denied()) return;
    triggerAlert(F("Test celebration"), false);
    web.send(200, "application/json", "{\"ok\":true}");
}

static void handleSettingsGet() {
    if (denied()) return;
    JsonDocument doc;
    doc["name"] = cfg.name;
    doc["mac"]  = devMac;
    doc["tz"]   = cfg.tz;
    doc["lock"] = cfg.pin[0] != '\0';       // never echo the PIN itself
    String out;
    serializeJson(doc, out);
    web.send(200, "application/json", out);
}

static void handleSettingsPost() {
    if (denied()) return;
    JsonDocument d;
    if (!readBody(d)) { web.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }

    // Only the everyday settings live here. Pairing itself - and the sync
    // server - are set once, in the web app and the setup portal, so the
    // lamp's own UI never has to talk about either.
    String name = String((const char *)(d["name"] | cfg.name));
    String tz   = String((const char *)(d["tz"]   | cfg.tz));

    if (name.length()) strlcpy(cfg.name, name.c_str(), sizeof(cfg.name));
    if (tz.length())   strlcpy(cfg.tz,   tz.c_str(),   sizeof(cfg.tz));

    // "pin" absent leaves it alone; "" clears it; anything else sets it.
    if (!d["pin"].isNull())
        strlcpy(cfg.pin, String((const char *)(d["pin"] | "")).c_str(), sizeof(cfg.pin));

    settingsSave();
    web.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
    rebootAt = millis() + 800;              // let the response flush first
}

static void handleOtaCheck() {
    if (denied()) return;
    /* This blocks the whole loop for up to OTA_HTTP_TIMEOUT_MS, which means
     * vibTask() cannot run - so park the motor first rather than leave it
     * energised for fifteen seconds. */
    vibStop();

    String latest, url;
    bool available = OTA::checkGitHubUpdate(latest, url);

    otaLatest = latest;
    otaUrl    = url;

    JsonDocument doc;
    doc["current"]   = FW_VERSION;
    doc["latest"]    = latest;
    doc["url"]       = url;
    doc["available"] = available;
    doc["notes"]     = OTA::lastNotes;
    doc["error"]     = OTA::lastError;
    String out;
    serializeJson(doc, out);
    web.send(200, "application/json", out);
}

static void handleOtaUpdate() {
    if (denied()) return;
    /* The URL is deliberately NOT taken from the request: only the one this
     * lamp itself read out of version.json is trusted. Otherwise anyone on the
     * Wi-Fi could POST a link and have the lamp flash their firmware. */
    if (otaUrl.isEmpty()) {
        web.send(409, "application/json", "{\"error\":\"run Check Update first\"}");
        return;
    }

    web.send(200, "application/json", "{\"ok\":true,\"msg\":\"updating\"}");
    otaPending = true;                       // flashed from loop()
}

static void handleWifiReset() {
    if (denied()) return;
    web.send(200, "application/json", "{\"ok\":true}");
    WiFiManager wm;
    wm.resetSettings();
    rebootAt = millis() + 800;
}

static void handleNotFound() {
    web.send(404, "application/json", "{\"error\":\"not found\"}");
}

static void webBegin() {
    web.on("/",              HTTP_GET,  handleRoot);
    web.on("/api/state",     HTTP_GET,  sendState);
    web.on("/api/control",   HTTP_POST, handleControl);
    web.on("/api/mood",      HTTP_POST, handleMood);
    web.on("/api/note",      HTTP_POST, handleNote);
    web.on("/api/sound",     HTTP_POST, handleSound);
    web.on("/api/vibrate",   HTTP_POST, handleVibrate);
    web.on("/api/dates",     HTTP_GET,  handleDatesGet);
    web.on("/api/dates",     HTTP_POST, handleDatesPost);
    web.on("/api/testalert", HTTP_POST, handleTestAlert);
    web.on("/api/settings",  HTTP_GET,  handleSettingsGet);
    web.on("/api/settings",  HTTP_POST, handleSettingsPost);
    web.on("/api/ota/check", HTTP_GET,  handleOtaCheck);
    web.on("/api/ota/update",HTTP_POST, handleOtaUpdate);
    web.on("/api/wifireset", HTTP_POST, handleWifiReset);
    web.onNotFound(handleNotFound);
    web.begin();
    Serial.println(F("[WEB] server on :80"));
}

/* =======================================================================
 *  Wi-Fi / captive portal / NTP / mDNS
 * ==================================================================== */

bool portalParamsSaved = false;
WiFiManagerParameter *pName, *pUrl, *pKey;
WiFiManagerParameter *pTz, *pPin;
WiFiManagerParameter *pSec1, *pSec3, *pSec4;   // raw-HTML section headings

static void onPortalSave() { portalParamsSaved = true; }

static void onPortalStart(WiFiManager *wm) {
    Serial.println(F("[WM] config portal up"));
    displaySplash(F("SETUP MODE"), String(AP_SSID));
}

static void startServices() {
    if (WiFi.status() != WL_CONNECTED) return;

    configTzTime(cfg.tz, NTP_SERVER_1, NTP_SERVER_2);

    if (!mdnsOk && MDNS.begin(cfg.name)) {
        MDNS.addService("http", "tcp", 80);
        mdnsOk = true;
        Serial.printf("[mDNS] http://%s.local\n", cfg.name);
    }

    if (!devMac[0]) { strlcpy(devMac, WiFi.macAddress().c_str(), sizeof(devMac)); computeMacKey(); }
    Fb::begin(cfg.firebaseApiKey, cfg.firebaseDatabaseUrl, devMacKey, cfg.deviceSecret);
}

static void wifiTask() {
    static uint32_t lastTry = 0;
    static bool wasUp = false;
    bool up = WiFi.status() == WL_CONNECTED;

    if (up && !wasUp) { startServices(); Serial.printf("[WIFI] %s\n", WiFi.localIP().toString().c_str()); }
    wasUp = up;

    if (!up && millis() - lastTry > WIFI_RETRY_MS) {
        lastTry = millis();
        WiFi.reconnect();
    }
}

/* =======================================================================
 *  setup / loop
 * ==================================================================== */

void setup() {
    Serial.begin(115200);
    bootMillis = millis();
    Serial.println("\n" FW_NAME " " FW_VERSION);

    // Pulldown, not plain INPUT: an unplugged TTP223 leaves the pin floating
    // and the lamp buzzes its partner at random.
    pinMode(PIN_TOUCH, INPUT_PULLDOWN);
    pinMode(PIN_VIBRATION, OUTPUT);
    vibDrive(false);

    settingsLoad();
    if (freshSecret) settingsSave();         // keep the device secret we just minted
    strlcpy(devMac, WiFi.macAddress().c_str(), sizeof(devMac));   // fixed by hardware, no Wi-Fi connection needed
    computeMacKey();

    strip.begin();
    strip.setBrightness(st.bright);
    strip.clear();
    strip.show();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
    if (oledOk) {
        oled.setTextWrap(false);
        displaySplash(F("Love Lamp"), String("v") + FW_VERSION);
    } else {
        Serial.println(F("[OLED] not found"));
    }

    audioBegin();

    /* ---- Wi-Fi + captive portal (blocking only during boot) ---------- */
    pSec1 = new WiFiManagerParameter(PORTAL_SEC_LAMP);          // raw-HTML heading
    pName = new WiFiManagerParameter("name", "Lamp name (x.local)", cfg.name,      sizeof(cfg.name) - 1);
    pTz   = new WiFiManagerParameter("tz",   "Time zone", cfg.tz,                  sizeof(cfg.tz) - 1);
    pSec3 = new WiFiManagerParameter(PORTAL_SEC_SYNC);
    pUrl  = new WiFiManagerParameter("fburl", "Firebase database URL", cfg.firebaseDatabaseUrl, sizeof(cfg.firebaseDatabaseUrl) - 1);
    pKey  = new WiFiManagerParameter("fbkey", "Firebase Web API key", cfg.firebaseApiKey, sizeof(cfg.firebaseApiKey) - 1);
    pSec4 = new WiFiManagerParameter(PORTAL_SEC_LOCK);
    pPin  = new WiFiManagerParameter("pin",   "PIN (blank = no lock)", cfg.pin,    PIN_MAX_LEN);

    WiFiManager wm;
    wm.setTitle("Love &#10084; Lamp");
    wm.setCustomHeadElement(PORTAL_HEAD);       // our skin, appended after the default style
    wm.setCustomMenuHTML(PORTAL_INTRO);         // rendered where "custom" sits in the menu
    std::vector<const char *> portalMenu = { "custom", "wifi", "info", "sep", "restart", "exit" };
    wm.setMenu(portalMenu);
    wm.setConfigPortalTimeout(AP_PORTAL_TIMEOUT_S);
    wm.setAPCallback(onPortalStart);
    wm.setSaveParamsCallback(onPortalSave);
    wm.addParameter(pSec1); wm.addParameter(pName); wm.addParameter(pTz);
    wm.addParameter(pSec3); wm.addParameter(pUrl); wm.addParameter(pKey);
    wm.addParameter(pSec4); wm.addParameter(pPin);

    if (!wm.autoConnect(AP_SSID, AP_PASSWORD)) {
        Serial.println(F("[WM] no Wi-Fi - running offline, will retry"));
    }

    if (portalParamsSaved) {
        strlcpy(cfg.name,     pName->getValue(), sizeof(cfg.name));
        strlcpy(cfg.firebaseDatabaseUrl, pUrl->getValue(), sizeof(cfg.firebaseDatabaseUrl));
        strlcpy(cfg.firebaseApiKey,      pKey->getValue(), sizeof(cfg.firebaseApiKey));
        strlcpy(cfg.tz,       pTz->getValue(),   sizeof(cfg.tz));
        strlcpy(cfg.pin,      pPin->getValue(),  sizeof(cfg.pin));
        settingsSave();
    }

    WiFi.setAutoReconnect(true);
    startServices();
    webBegin();

    screenAt = millis();
    Serial.printf("[CFG] %s mac=%s firebase=%s\n", cfg.name, devMac, cfg.firebaseDatabaseUrl);
}

void loop() {
    web.handleClient();
    wifiTask();
    firebaseTask();
    touchTask();
    vibTask();
    ledTask();
    displayTask();
    anniversaryTask();
    stateFlushTask();
    heapTask();

    if (otaPending) {
        otaPending = false;
        displaySplash(F("UPDATING..."), otaLatest.length() ? otaLatest : String("firmware"));
        vibStop();
        strip.clear();
        strip.show();
        // No connection to close - our row simply goes stale to the partner
        // once polling stops, the same way a crash or power loss would.
        OTA::performOTAUpdate(otaUrl.c_str());          // reboots on success
        displaySplash(F("UPDATE FAILED"), OTA::lastError);
    }

    if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) ESP.restart();
}

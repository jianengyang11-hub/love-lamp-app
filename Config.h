#pragma once
#include <Arduino.h>

/* =========================================================================
 *  Config.h - pinout, defaults, firmware identity, OTA endpoints
 * ========================================================================= */

/* ------------------------- Firmware identity ---------------------------- */
#define FW_NAME              "LoveLamp"
#define FW_VERSION           "1.0.0"          // keep in sync with version.json

/* ------------------------- GitHub OTA endpoints ------------------------- */
// version.json lives on the raw branch, e.g.
// { "version": "1.0.1", "url": "https://github.com/<user>/<repo>/releases/download/v1.0.1/firmware.bin", "notes": "..." }
#define OTA_GITHUB_USER      "your-github-user"
#define OTA_GITHUB_REPO      "esp32-lovelamp"
#define OTA_GITHUB_BRANCH    "main"
#define OTA_VERSION_URL      "https://raw.githubusercontent.com/" OTA_GITHUB_USER "/" OTA_GITHUB_REPO "/" OTA_GITHUB_BRANCH "/firmware/version.json"
// Fallback used when version.json carries no "url" field:
#define OTA_BIN_URL_FALLBACK "https://raw.githubusercontent.com/" OTA_GITHUB_USER "/" OTA_GITHUB_REPO "/" OTA_GITHUB_BRANCH "/firmware/firmware.bin"
#define OTA_HTTP_TIMEOUT_MS  15000

/* ------------------------------ Pinout ---------------------------------- */
#define PIN_LED_DATA         15      // WS2812B DIN
#define PIN_TOUCH            4       // TTP223 OUT (active HIGH)
#define PIN_I2C_SDA          21      // OLED SDA
#define PIN_I2C_SCL          22      // OLED SCL
#define PIN_VIBRATION        13      // vibration motor module (PWM capable)
#define PIN_DFP_RX           16      // ESP32 RX2  <- DFPlayer TX
#define PIN_DFP_TX           17      // ESP32 TX2  -> DFPlayer RX (use 1k series resistor)

/* ------------------------------ LED strip ------------------------------- */
#define LED_COUNT            24
#define LED_TYPE             (NEO_GRB + NEO_KHZ800)
#define LED_FRAME_MS         20      // ~50 fps animation tick
#define LED_DEFAULT_BRIGHT   160

/* ------------------------------- OLED ----------------------------------- */
#define OLED_WIDTH           128
#define OLED_HEIGHT          64
#define OLED_I2C_ADDR        0x3C    // 0x3D on some SH1106 boards
#define OLED_RESET_PIN       -1
#define SCREEN_ROTATE_MS     5000    // rotate info screens every 5 s
#define SCREEN_FRAME_MS      60      // redraw rate (drives text scrolling)

/* ---------------------------- Touch sensor ------------------------------ */
#define TOUCH_DEBOUNCE_MS    50
#define TOUCH_LONGPRESS_MS   1200

/* -------------------------- Vibration engine ---------------------------- */
// Realistic "heartbeat": 100 ON -> 100 OFF -> 100 ON -> 600 OFF, x3
#define VIB_BEAT_ON_MS       100
#define VIB_BEAT_GAP_MS      100
#define VIB_BEAT_REST_MS     600
#define VIB_REPEATS          3
#define VIB_POWER            255     // PWM duty 0-255

/* ------------------------------- Audio ---------------------------------- */
#define TRACK_CHIME          1       // touch / love note received
#define TRACK_LULLABY        2       // mood = SLEEPING
#define TRACK_CELEBRATION    3       // anniversary alert
#define DFP_DEFAULT_VOLUME   22      // 0..30, adjustable in the Web UI
#define DFP_VOLUME_MAX       30

/* ------------------------------- Clock ---------------------------------- */
#define NTP_SERVER_1         "pool.ntp.org"
#define NTP_SERVER_2         "time.nist.gov"
// POSIX TZ string, editable per lamp in the portal and the Setup tab - two
// lamps in different countries each celebrate on their own local date.
// Bangkok "ICT-7" · Tokyo "JST-9" · London "GMT0BST,M3.5.0/1,M10.5.0"
// New York "EST5EDT,M3.2.0,M11.1.0" · Sydney "AEST-10AEDT,M10.1.0,M4.1.0/3"
#define DEF_TZ               "ICT-7"

/* --------------------------- Anniversaries ------------------------------ */
#define MAX_SPECIAL_DATES    12
#define ALERT_DURATION_MS    60000UL          // celebration lasts 60 s

/* --------------------------- Networking --------------------------------- */
#define AP_SSID              "LoveLamp_Setup"
#define AP_PASSWORD          "loveyou123"     // >= 8 chars, or "" for open AP
#define AP_PORTAL_TIMEOUT_S  180
#define WIFI_RETRY_MS        30000

#define DEF_DEVICE_NAME      "lovelamp"       // -> http://lovelamp.local

/* --------------------------- Firebase sync -------------------------------- */
// Lamp-to-lamp sync, and pairing itself, go through a Firebase Realtime
// Database project (see firebase/database.rules.json) instead of an MQTT
// broker or a portal-typed Pair ID. No public default exists - a Firebase
// project is yours alone, so the API key and database URL are set once in
// the portal, same as the old broker fields.
#define DEF_FIREBASE_API_KEY      ""   // Firebase console -> Project settings -> General
#define DEF_FIREBASE_DATABASE_URL ""   // e.g. https://xxxx-default-rtdb.firebaseio.com
// Shown on the onboarding OLED screen while unpaired, e.g. "yourapp.pages.dev".
// Purely informational text - leave blank and it just says "the Love Lamp app".
#define WEBAPP_HOST                ""

// This lamp's own identity with Firebase: a random token it mints itself on
// first boot (see mintDeviceSecret() in the .ino), used as the password for
// a Firebase Auth account under a MAC-derived email (see FirebaseSync.h) -
// the same role the old "x-device-secret" header played against Supabase,
// just carried through Firebase's own Auth system instead of a custom header.
#define DEVICE_SECRET_BYTES        24  // -> 48 hex characters

// While unpaired: how often the background task re-checks whether this MAC
// has been claimed and paired yet. Fast, because a human is actively
// waiting on the other end (the web app) for this to go through.
#define PROVISION_POLL_MS       4000
// Once paired: how often it re-confirms pairing is still current (catches
// an "Unpair" from the web app) and refreshes devices/{mac}/lastSeenAt. Slow,
// because the partner's *lamp_sync* row is now pushed instantly over the
// Realtime Database stream (see FirebaseSync.h) - this cadence only matters
// for the provisioning bookkeeping, not for how fast a colour change arrives.
#define DEVICE_CHECKIN_MS      60000

// lamp_sync: re-upsert with no changes, so our row doesn't go stale in the
// partner's eyes (see PARTNER_STALE_MS) while we're perfectly online but idle.
#define FIREBASE_HEARTBEAT_MS  45000

// How long a lamp still counts as "there" on the strength of its last
// lamp_sync update alone - there is no broker to notice a dead connection
// and flip a flag, so staleness of the partner's row is the only presence
// signal there is.
#define PARTNER_STALE_MS      180000UL

/* ------------------------------ Web UI ---------------------------------- */
// Optional HTTP basic-auth PIN. Empty (the default) leaves the UI open, which
// is the right call on a home network. Set one from the Setup tab or the
// portal if the lamp shares Wi-Fi with people you would rather not have
// reading the love notes. Basic auth over plain HTTP is only as private as
// the LAN - it stops housemates, not someone sniffing the wire.
#define WEB_AUTH_USER        "lamp"
#define PIN_MAX_LEN          15

/* ------------------------------ Limits ---------------------------------- */
#define NOTE_MAX_LEN         96
#define ALERT_MAX_LEN        64
#define HEAP_LOG_MS          300000UL         // heap report to serial, 5 min
#define PREF_NAMESPACE       "lovelamp"

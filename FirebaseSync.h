#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "Config.h"

/* =========================================================================
 *  FirebaseSync.h - device provisioning + lamp-to-lamp sync, both over
 *  Firebase Realtime Database, using the "Firebase ESP Client" library by
 *  mobizt (Arduino Library Manager name: "Firebase ESP Client"; GitHub:
 *  mobizt/Firebase-ESP-Client; header Firebase_ESP_Client.h - sometimes
 *  referred to by the name of its ESP32-only predecessor, "Firebase-ESP32").
 *
 *  Everything here runs on one FreeRTOS task on core 0, entirely off
 *  loop() - Firebase Auth sign-in/refresh and Realtime Database calls block
 *  for the better part of a second each, and loop() must not: LED frames,
 *  vibration timing and touch debounce all live there. The .ino talks to
 *  this task through a small set of thread-safe accessors and two
 *  mailboxes (OutgoingState / IncomingState) guarded by one mutex.
 *
 *  Two phases, both handled by the same task loop:
 *
 *   PROVISIONING - this lamp doesn't yet know its own `devices` row, or
 *   knows it but isn't paired. It self-registers under its MAC address the
 *   first time it's ever seen (an unclaimed row with no user attached), then
 *   polls that same row waiting for the Love Lamp web app to claim it and
 *   pair it with a partner.
 *
 *   PAIRED - this lamp has a pairId and a role. Before it can read or write
 *   any lamp_sync data it first claims its own pairs/{pairId}/deviceAuth{A,B}
 *   slot (claimPairSlot()) - the field the rules actually check to
 *   authorize a device, as opposed to the human owner/partner uid a browser
 *   session authorizes with. Once claimed, it keeps a live Realtime
 *   Database *stream* open on /lamp_sync/{pairId}/{partnerRole} - not a
 *   poll loop - so a change from the partner's lamp or their dashboard
 *   arrives the instant Firebase delivers it. sendUpdate() upserts its own
 *   /lamp_sync/{pairId}/{myRole} node whenever the .ino calls it - on every
 *   real change, and (see the .ino's firebaseTask()) periodically with no
 *   changes, so our leaf doesn't go stale in the partner's eyes while idle.
 *   This task also keeps re-checking its own `devices` row on a slow
 *   cadence (DEVICE_CHECKIN_MS), both to refresh lastSeenAt and to notice
 *   if the web app unpairs it - at which point it falls back to
 *   PROVISIONING and the OLED goes back to onboarding.
 *
 *  Device identity: the web app's humans sign in with Firebase Auth's Phone
 *  provider (OTP) - a lamp obviously can't receive an SMS, so it uses a
 *  *different* Firebase Auth provider for itself: Email/Password, under a
 *  fixed email derived from its own MAC address and a random
 *  DEVICE_SECRET_BYTES secret (mintDeviceSecret() in the .ino) as the
 *  password - persisted in NVS, exactly like the old Supabase
 *  "x-device-secret" header was. Every later boot just signs *in* with the
 *  same email/password, which Firebase Auth always resolves to the same
 *  stable uid - unlike Anonymous auth, which mints a new uid every session
 *  with no easy way to resume the old one. The Realtime Database rules
 *  (database.rules.json) tell the two providers apart with
 *  `auth.token.phone_number != null` - true only for the humans - so a
 *  device's email/password session is never mistaken for a pair owner or
 *  partner, and a phone session can never impersonate a device. See
 *  firebase/README.md's "Why two different Auth providers".
 *
 *  Every lamp_sync write is a full snapshot - vib/track/alert are sent as
 *  explicit false/0/"" rather than omitted, because an update only touches
 *  the fields present in its body. Omitting them would leave a sticky true
 *  sitting in the node, ready to misfire on the next unrelated update.
 * ========================================================================= */

namespace Fb {

struct OutgoingState {
    bool    power;
    char    color[8];                  // "#rrggbb"
    uint8_t bright;
    char    mood[8];
    char    note[NOTE_MAX_LEN + 1];
    bool    vib;
    uint8_t track;
    char    alert[ALERT_MAX_LEN + 1];
};

struct IncomingState {
    bool    power;
    char    color[8];
    uint8_t bright;
    char    mood[8];
    char    note[NOTE_MAX_LEN + 1];
    bool    vib;
    uint8_t track;
    char    alert[ALERT_MAX_LEN + 1];
};

namespace {

/* ---- Firebase library objects ---- */
FirebaseData s_fbdo;          // one-off get/set/updateNode calls
FirebaseData s_streamFbdo;    // dedicated to the realtime stream (the library
                               // requires a separate FirebaseData per concurrent operation)
FirebaseAuth s_auth;
FirebaseConfig s_config;

/* ---- fixed identity, set once by begin() ---- */
char s_mac[13]    = "";        // 12 hex chars, no colons
char s_email[48]  = "";        // "device-<mac>@lovelamp.device"
char s_secret[DEVICE_SECRET_BYTES * 2 + 1] = "";

SemaphoreHandle_t s_mutex = nullptr;
bool              s_started = false;

/* ---- provisioning state, updated by the task, read by the .ino ---- */
char s_pairId[32] = "";
char s_role       = 0;         // 'A' / 'B' / 0 = not paired
bool s_claimed    = false;     // devices/{mac}/userId is set
bool s_paired     = false;
bool s_authTrouble = false;    // best-effort "can't sign in" signal - see taskFn()

/* ---- lamp_sync mailboxes ---- */
OutgoingState s_outbox;
bool          s_outboxPending = false;

IncomingState s_inbox;
bool          s_inboxPending = false;

uint32_t s_lastOkMs = 0;       // last successful Firebase round-trip (upsert, checkin, or stream event)

/* ---- realtime stream bookkeeping (task-thread only, no mutex needed) ---- */
bool s_streamActive  = false;
char s_streamPairId[32] = "";
char s_streamRole      = 0;

/* -------------------------------------------------------------------------
 *  Realtime stream - fires on the library's own background task whenever
 *  the partner's lamp_sync node changes. No polling.
 * ---------------------------------------------------------------------- */

void streamCallback(FirebaseStream data) {
    // Every write this project ever makes to a lamp_sync leaf is one
    // updateNode() touching the whole set of fields together, so the event
    // we care about is always the whole object at the stream's own base
    // path ("/") - both the initial full snapshot Firebase sends right
    // after beginStream() and every later change arrive this same shape.
    if (data.dataPath() != "/" || data.dataType() != "json") return;

    JsonDocument doc;
    if (deserializeJson(doc, data.jsonString())) {
        Serial.println(F("[FB] bad stream json"));
        return;
    }

    IncomingState in;
    in.power = doc["power"] | true;
    strlcpy(in.color, doc["color"] | "#ffffff", sizeof(in.color));
    in.bright = doc["bright"] | LED_DEFAULT_BRIGHT;
    strlcpy(in.mood, doc["mood"] | "normal", sizeof(in.mood));
    strlcpy(in.note, doc["note"] | "", sizeof(in.note));
    in.vib = doc["vib"] | false;
    in.track = doc["track"] | 0;
    strlcpy(in.alert, doc["alert"] | "", sizeof(in.alert));

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50))) {
        s_inbox = in;
        s_inboxPending = true;
        s_lastOkMs = millis();
        xSemaphoreGive(s_mutex);
    }
}

void streamTimeoutCallback(bool timeout) {
    // The library resumes the stream on its own after a timeout - this is
    // just visibility, not an error to react to.
    if (timeout) Serial.println(F("[FB] stream timeout, resuming"));
}

/* (Re)points the realtime stream at the partner's current lamp_sync leaf.
 * A no-op if it's already pointed there. */
bool ensureStream(const char *pairId, char partnerRole) {
    if (s_streamActive && strcmp(s_streamPairId, pairId) == 0 && s_streamRole == partnerRole) return true;

    char path[48];
    snprintf(path, sizeof(path), "/lamp_sync/%s/%c", pairId, partnerRole);

    if (!Firebase.RTDB.beginStream(&s_streamFbdo, path)) {
        Serial.printf("[FB] beginStream failed: %s\n", s_streamFbdo.errorReason().c_str());
        return false;
    }
    Firebase.RTDB.setStreamCallback(&s_streamFbdo, streamCallback, streamTimeoutCallback);

    strlcpy(s_streamPairId, pairId, sizeof(s_streamPairId));
    s_streamRole = partnerRole;
    s_streamActive = true;
    Serial.printf("[FB] streaming %s\n", path);
    return true;
}

/* -------------------------------------------------------------------------
 *  lamp_sync upsert - pair_id/role are always in the path, never sent as
 *  fields, so a write can never land anywhere but this lamp's own leaf.
 * ---------------------------------------------------------------------- */
bool doUpsert(const char *pairId, char role, const OutgoingState &s) {
    if (!pairId[0] || WiFi.status() != WL_CONNECTED) return false;

    FirebaseJson json;
    json.set("deviceId", s_mac);
    json.set("power", s.power);
    json.set("color", s.color);
    json.set("bright", s.bright);
    json.set("mood", s.mood);
    json.set("note", s.note);
    json.set("vib", s.vib);
    json.set("track", s.track);
    json.set("alert", s.alert);
    json.set("updatedAt/.sv", "timestamp");   // RTDB server-timestamp placeholder

    char path[48];
    snprintf(path, sizeof(path), "/lamp_sync/%s/%c", pairId, role);

    if (!Firebase.RTDB.updateNode(&s_fbdo, path, &json)) {
        Serial.printf("[FB] upsert failed: %s\n", s_fbdo.errorReason().c_str());
        return false;
    }
    return true;
}

/* One-time claim of this device's own slot on the pair it belongs to -
 * pairs/{pairId}/deviceAuthA (or B) = this device's own Firebase Auth uid.
 * The lamp_sync read/write rules authorize a device by checking that exact
 * field (see database.rules.json), so until this succeeds the device can
 * see neither its own nor its partner's lamp_sync leaf. A targeted
 * single-field write, not a whole-object one - the rules only grant write
 * access to *this* field for a device, not the rest of the pairs node.
 *
 * The rule only allows setting this field once (its old value must be
 * empty) - correct for a genuine first claim, but this function's own
 * caller can't tell "never claimed" from "already claimed, on a previous
 * boot" without asking first. Skipping straight to setString() on every
 * boot would get permission-denied forever after the first success, since
 * the slot is never empty again. So: read first: if it already reads back
 * as our own uid, we're done, no write needed or attempted. */
bool claimPairSlot(const char *pairId, char role) {
    char path[48];
    snprintf(path, sizeof(path), "/pairs/%s/deviceAuth%c", pairId, role);

    if (Firebase.RTDB.getString(&s_fbdo, path) && s_fbdo.stringData() == s_auth.token.uid) {
        return true;   // already claimed by us, from an earlier boot or cycle
    }

    if (!Firebase.RTDB.setString(&s_fbdo, path, s_auth.token.uid.c_str())) {
        Serial.printf("[FB] claim pair slot: %s\n", s_fbdo.errorReason().c_str());
        return false;
    }
    Serial.printf("[FB] claimed pairs/%s/deviceAuth%c\n", pairId, role);
    return true;
}

/* -------------------------------------------------------------------------
 *  Provisioning - looks up this lamp's own devices/{mac} node, registers it
 *  if it doesn't exist yet, reports whether it's claimed/paired, and
 *  heartbeats lastSeenAt while we're at it.
 * ---------------------------------------------------------------------- */
bool doCheckin(char *ioPairId, size_t pairCap, char &ioRole, bool &ioPaired, bool &ioClaimed) {
    if (!Firebase.ready()) return false;

    char path[24];
    snprintf(path, sizeof(path), "/devices/%s", s_mac);

    if (!Firebase.RTDB.getJSON(&s_fbdo, path)) {
        // Firebase reports a non-existent path as a failed get() - treat
        // that as "not registered yet under this identity" and self-register.
        FirebaseJson json;
        json.set("mac", s_mac);
        json.set("authUid", s_auth.token.uid.c_str());
        json.set("name", "My Lamp");
        json.set("createdAt/.sv", "timestamp");
        json.set("lastSeenAt/.sv", "timestamp");

        if (!Firebase.RTDB.setJSON(&s_fbdo, path, &json)) {
            Serial.printf("[FB] self-register failed: %s\n", s_fbdo.errorReason().c_str());
            return false;
        }
        ioPairId[0] = '\0';
        ioRole      = 0;
        ioPaired    = false;
        ioClaimed   = false;
        Serial.println(F("[FB] self-registered"));
        return true;
    }

    JsonDocument doc;
    if (deserializeJson(doc, s_fbdo.payload())) {
        Serial.println(F("[FB] bad devices json"));
        return false;
    }

    ioClaimed = !doc["userId"].isNull();

    const char *pairId = doc["pairId"] | "";
    const char *role   = doc["role"] | "";
    if (pairId[0] && role[0]) {
        strlcpy(ioPairId, pairId, pairCap);
        ioRole   = role[0];
        ioPaired = (ioRole == 'A' || ioRole == 'B');
    } else {
        ioPairId[0] = '\0';
        ioRole      = 0;
        ioPaired    = false;
    }

    FirebaseJson heartbeat;
    heartbeat.set("lastSeenAt/.sv", "timestamp");
    Firebase.RTDB.updateNode(&s_fbdo, path, &heartbeat);   // best-effort

    return true;
}

/* -------------------------------------------------------------------------
 *  The task itself
 * ---------------------------------------------------------------------- */
void taskFn(void *) {
    char     pairId[32] = "";
    char     role        = 0;
    bool     paired      = false;
    bool     claimed     = false;
    bool     slotClaimed = false;   // pairs/{pairId}/deviceAuth{role} - see claimPairSlot()

    uint32_t lastCheckin   = 0;
    uint32_t notReadyCount = 0;

    // Sign-up is expected to fail with EMAIL_EXISTS on every boot after the
    // very first one - that is the normal, healthy case, not an error to
    // react to. begin() performs the actual sign-*in* every time, always
    // resolving to the same uid for this email/password pair.
    s_auth.user.email    = s_email;
    s_auth.user.password = s_secret;
    Firebase.signUp(&s_config, &s_auth, s_email, s_secret);
    Firebase.begin(&s_config, &s_auth);
    Firebase.reconnectWiFi(true);

    for (;;) {
        if (!Firebase.ready()) {
            // Either still connecting, or - if this persists - the email
            // exists under a *different* password (an NVS wipe minted a new
            // secret while the old devices/{mac} row, and Firebase Auth
            // account, still remember the old one). We can't cleanly
            // distinguish "still connecting" from "wrong secret" through
            // this library's async ready-state alone, so treat a run of
            // failures as a possible identity conflict and surface it - see
            // hasAuthTrouble() - while keeping I/O paused either way.
            notReadyCount++;
            if (notReadyCount >= 5 && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50))) {
                s_authTrouble = true;
                xSemaphoreGive(s_mutex);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (notReadyCount > 0) {
            notReadyCount = 0;
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50))) {
                s_authTrouble = false;
                xSemaphoreGive(s_mutex);
            }
        }

        // 1. Drain the outbox - only meaningful once paired.
        OutgoingState toSend;
        bool doSend = false;
        if (xSemaphoreTake(s_mutex, portMAX_DELAY)) {
            if (s_outboxPending) { toSend = s_outbox; doSend = true; s_outboxPending = false; }
            xSemaphoreGive(s_mutex);
        }
        if (doSend && paired && doUpsert(pairId, role, toSend)) {
            if (xSemaphoreTake(s_mutex, portMAX_DELAY)) { s_lastOkMs = millis(); xSemaphoreGive(s_mutex); }
        }

        // 2. Checkin - fast while unpaired (a human is waiting on the web
        // app), slow once paired (just a heartbeat + "am I still paired?").
        uint32_t checkinInterval = paired ? DEVICE_CHECKIN_MS : PROVISION_POLL_MS;
        if (millis() - lastCheckin >= checkinInterval) {
            lastCheckin = millis();
            bool wasPaired = paired;
            char prevPairId[32];
            strlcpy(prevPairId, pairId, sizeof(prevPairId));

            if (doCheckin(pairId, sizeof(pairId), role, paired, claimed)) {
                if (xSemaphoreTake(s_mutex, portMAX_DELAY)) {
                    s_lastOkMs = millis();
                    strlcpy(s_pairId, pairId, sizeof(s_pairId));
                    s_role    = role;
                    s_paired  = paired;
                    s_claimed = claimed;
                    xSemaphoreGive(s_mutex);
                }

                if (paired && (!wasPaired || strcmp(prevPairId, pairId) != 0)) {
                    slotClaimed = false;   // a fresh (or changed) pairing needs a fresh claim
                }
                if (wasPaired && !paired) {
                    s_streamActive = false;   // ensureStream() reopens once re-paired
                    slotClaimed = false;
                    Serial.println(F("[FB] no longer paired - back to onboarding"));
                }

                // The lamp_sync read/write rules authorize a device by its
                // pairs/{pairId}/deviceAuth{role} slot (see
                // database.rules.json) - claim it before trying to stream
                // the partner's leaf, or that read has nothing to authorize
                // it with yet. Retried every checkin cycle until it sticks.
                if (paired && !slotClaimed) {
                    slotClaimed = claimPairSlot(pairId, role);
                }
                if (paired && slotClaimed) {
                    char partnerRole = (role == 'A') ? 'B' : 'A';
                    ensureStream(pairId, partnerRole);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

} // anonymous namespace

/** Starts the background task. Safe to call again after a Wi-Fi reconnect -
 *  only the first call actually spawns the task. */
inline void begin(const char *apiKey, const char *databaseUrl, const char *mac, const char *deviceSecret) {
    strlcpy(s_mac, mac, sizeof(s_mac));
    strlcpy(s_secret, deviceSecret, sizeof(s_secret));
    snprintf(s_email, sizeof(s_email), "device-%s@lovelamp.device", mac);

    s_config.api_key      = apiKey;
    s_config.database_url = databaseUrl;

    if (s_started) return;
    s_started = true;
    s_mutex = xSemaphoreCreateMutex();
    // A larger stack than SupaSync.h's Supabase-era task needed: the
    // Firebase client library's own TLS/JSON machinery runs on this task too.
    xTaskCreatePinnedToCore(taskFn, "fbSync", 16384, nullptr, 1, nullptr, 0);
}

/** Queues the lamp's current state for the next upsert. Thread-safe and
 *  never touches the network itself - the background task sends it on its
 *  own tick. A no-op while unpaired. */
inline void sendUpdate(const OutgoingState &s) {
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50))) {
        s_outbox = s;
        s_outboxPending = true;
        xSemaphoreGive(s_mutex);
    }
}

/** Drains one partner update if the background task has one queued. */
inline bool pollIncoming(IncomingState &out) {
    if (!s_mutex) return false;
    bool got = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50))) {
        if (s_inboxPending) { out = s_inbox; got = true; s_inboxPending = false; }
        xSemaphoreGive(s_mutex);
    }
    return got;
}

inline bool isPaired() {
    if (!s_mutex) return false;
    bool p = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50))) { p = s_paired; xSemaphoreGive(s_mutex); }
    return p;
}

inline bool isClaimed() {
    if (!s_mutex) return false;
    bool c = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50))) { c = s_claimed; xSemaphoreGive(s_mutex); }
    return c;
}

/** Best-effort "can't sign in" signal - see the comment in taskFn(). Unlike
 *  the old Supabase transport's crisp HTTP 409, this library's async
 *  ready-state can't cleanly distinguish "still connecting" from "the
 *  device secret doesn't match what this MAC registered under before" -
 *  this is a heuristic, not a certainty. */
inline bool hasAuthTrouble() {
    if (!s_mutex) return false;
    bool t = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50))) { t = s_authTrouble; xSemaphoreGive(s_mutex); }
    return t;
}

inline char role() {
    if (!s_mutex) return 0;
    char r = 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50))) { r = s_role; xSemaphoreGive(s_mutex); }
    return r;
}

/** True if the background task completed a Firebase round-trip recently -
 *  the OLED's connectivity dot. */
inline bool healthy() {
    if (!s_mutex) return false;
    uint32_t last = 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50))) { last = s_lastOkMs; xSemaphoreGive(s_mutex); }
    return last && (millis() - last) < (uint32_t)(DEVICE_CHECKIN_MS + FIREBASE_HEARTBEAT_MS);
}

} // namespace Fb

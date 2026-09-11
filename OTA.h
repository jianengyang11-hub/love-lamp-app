#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include "Config.h"

/* =========================================================================
 *  OTA.h - GitHub-hosted firmware update (check + apply)
 *
 *  checkGitHubUpdate() : fetches version.json over HTTPS, compares semver
 *  performOTAUpdate()  : streams firmware.bin through httpUpdate, reboots
 *
 *  Both are header-only (inline) so this file can be included from anywhere.
 * ========================================================================= */

namespace OTA {

/* Last human-readable error, for surfacing in the Web UI. */
static String lastError = "";
/* Release notes taken from version.json on the last successful check. */
static String lastNotes = "";
/* (static, not inline: the Arduino-ESP32 core still compiles as gnu++11) */

/* --- semver-ish compare: returns >0 if a > b, 0 if equal, <0 if a < b ---- */
inline int compareVersion(const String &a, const String &b) {
    int ia = 0, ib = 0;
    while (ia < (int)a.length() || ib < (int)b.length()) {
        long na = 0, nb = 0;
        while (ia < (int)a.length() && isDigit(a[ia])) na = na * 10 + (a[ia++] - '0');
        while (ib < (int)b.length() && isDigit(b[ib])) nb = nb * 10 + (b[ib++] - '0');
        if (na != nb) return (na > nb) ? 1 : -1;
        while (ia < (int)a.length() && !isDigit(a[ia])) ia++;
        while (ib < (int)b.length() && !isDigit(b[ib])) ib++;
    }
    return 0;
}

/* Strips a leading 'v'/'V' so "v1.2.3" and "1.2.3" compare equal. */
inline String normalizeVersion(String v) {
    v.trim();
    if (v.length() && (v[0] == 'v' || v[0] == 'V')) v.remove(0, 1);
    return v;
}

/* -------------------------------------------------------------------------
 *  Fetch <OTA_VERSION_URL> and report the published version + binary URL.
 *  Returns true when a NEWER version than FW_VERSION is available.
 *  latestVersion / binUrl are filled in whenever the JSON parses, even if
 *  the published build is not newer (so the UI can say "you are up to date").
 * ---------------------------------------------------------------------- */
inline bool checkGitHubUpdate(String &latestVersion, String &binUrl) {
    lastError = "";
    lastNotes = "";
    latestVersion = "";
    binUrl = "";

    if (WiFi.status() != WL_CONNECTED) {
        lastError = "WiFi not connected";
        return false;
    }

    WiFiClientSecure client;
    // GitHub rotates its CA chain; pin a root CA here if you need strict
    // validation. setInsecure() keeps the transport encrypted but unverified.
    client.setInsecure();
    client.setTimeout(OTA_HTTP_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setConnectTimeout(OTA_HTTP_TIMEOUT_MS);
    http.setTimeout(OTA_HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent(FW_NAME "/" FW_VERSION);

    if (!http.begin(client, OTA_VERSION_URL)) {
        lastError = "Bad version URL";
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        lastError = "HTTP " + String(code);
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        lastError = String("Bad version.json: ") + err.c_str();
        return false;
    }

    latestVersion = normalizeVersion(doc["version"] | "");
    binUrl        = String(doc["url"] | OTA_BIN_URL_FALLBACK);
    lastNotes     = String(doc["notes"] | "");

    if (latestVersion.isEmpty()) {
        lastError = "version.json has no \"version\"";
        return false;
    }

    return compareVersion(latestVersion, normalizeVersion(FW_VERSION)) > 0;
}

/* -------------------------------------------------------------------------
 *  Download and flash firmware.bin. On success the ESP32 reboots inside
 *  httpUpdate.update() and this function never returns.
 * ---------------------------------------------------------------------- */
inline bool performOTAUpdate(const char *binUrl) {
    lastError = "";

    if (WiFi.status() != WL_CONNECTED) {
        lastError = "WiFi not connected";
        return false;
    }
    if (binUrl == nullptr || strlen(binUrl) < 8) {
        lastError = "Empty firmware URL";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(OTA_HTTP_TIMEOUT_MS / 1000);

    httpUpdate.rebootOnUpdate(true);
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    Serial.printf("[OTA] downloading %s\n", binUrl);
    t_httpUpdate_return ret = httpUpdate.update(client, String(binUrl), FW_VERSION);

    switch (ret) {
        case HTTP_UPDATE_OK:            // device reboots before reaching here
            return true;
        case HTTP_UPDATE_NO_UPDATES:
            lastError = "Server reports no update";
            break;
        case HTTP_UPDATE_FAILED:
        default:
            lastError = "Update failed (" + String(httpUpdate.getLastError()) + "): " +
                        httpUpdate.getLastErrorString();
            break;
    }
    Serial.printf("[OTA] %s\n", lastError.c_str());
    return false;
}

} // namespace OTA

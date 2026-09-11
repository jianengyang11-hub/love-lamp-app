#pragma once
#include <Arduino.h>
#include <pgmspace.h>

/* =========================================================================
 *  PortalPage.h - skin for the WiFiManager captive portal.
 *
 *  WiFiManager emits its own <style> first and then appends whatever
 *  setCustomHeadElement() holds, so these rules simply land last and win.
 *  Same palette and same display face as WebPage.h, so the setup screen and
 *  the lamp UI read as one product.
 *
 *  On ESP32 PROGMEM data sits in memory-mapped flash, so these strings can
 *  be handed straight to WiFiManager as plain const char*.
 * ========================================================================= */

static const char PORTAL_HEAD[] PROGMEM = R"HEAD(
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Instrument+Serif&display=swap">
<style>
:root{color-scheme:dark}
body{background:#14090f;color:#f7eaef;margin:0;padding:28px 16px 48px;text-align:center;
font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;font-size:15px;
line-height:1.5;min-height:100vh}
.wrap{display:inline-block;text-align:left;width:100%;max-width:420px;min-width:0}
h1{font:400 32px/1 "Instrument Serif",Georgia,serif;margin:0 0 20px;text-align:center;
letter-spacing:.4px}
h3{font-size:12px;text-transform:uppercase;letter-spacing:1.6px;color:#a98a98;
margin:24px 0 2px;font-weight:400}
form{background:#1e1119;border:1px solid #33202b;border-radius:16px;padding:18px;margin:0 0 12px}
input,select,textarea{width:100%;background:#14090f;border:1px solid #33202b;color:#f7eaef;
border-radius:11px;padding:11px;font:inherit;margin:6px 0 2px}
input:focus,select:focus{outline:0;border-color:#ff5c8a}
input[type=checkbox]{width:auto;float:left;margin:6px 8px 0 0;accent-color:#ff5c8a}
button,input[type=submit]{width:100%;border:1px solid #33202b;border-radius:13px;padding:14px;
font:inherit;font-weight:500;background:none;color:#f7eaef;line-height:1.3;cursor:pointer;
margin-top:14px;-webkit-appearance:none}
.wrap form:nth-of-type(1) button,.wrap form:nth-of-type(1) input[type=submit]{
border:0;background:#ff5c8a;color:#2a0a14;font-weight:600}
button:active{transform:scale(.985)}
a{color:#ff5c8a;text-decoration:none}
a:hover{text-decoration:underline}
.wrap>div>a{display:inline-block;padding:11px 0;font-size:15px}
.q{float:right;color:#a98a98;font-size:12px;padding-top:13px;width:64px;text-align:right}
.msg{background:#1e1119;border:1px solid #33202b;border-left:2px solid #ff5c8a;
border-radius:12px;padding:14px 16px;margin:0 0 14px;font-size:14px;color:#e8d5de}
.table{width:100%;border-collapse:collapse;font-size:13px}
.table td{padding:9px 6px;text-align:left;border-bottom:1px solid #33202b;color:#e8d5de}
.table td:first-child{color:#a98a98}
.table th{padding:9px 6px;text-align:left;color:#a98a98;font-size:11px;font-weight:400;
text-transform:uppercase;letter-spacing:1.4px;background:none;border-bottom:1px solid #33202b}
.lamp{text-align:center;color:#a98a98;font-size:13px;margin:-8px 0 20px}
.lamp b{display:block;color:#f7eaef;font:400 19px/1.3 "Instrument Serif",Georgia,serif;
margin-bottom:5px}
.hint{color:#a98a98;font-size:12px;margin:5px 0 0}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
</style>)HEAD";

/* Sits at the top of the portal's landing page (menu token "custom"). */
static const char PORTAL_INTRO[] PROGMEM = R"MENU(<div class="lamp">
<b>Let's get this lamp online</b>
Pick your Wi-Fi below. Pairing itself happens afterwards, in the
Love Lamp web app - once this lamp is online, its OLED shows its
MAC address and asks to be registered there.</div>)MENU";

/* Section headings injected between parameters as raw-HTML params. */
static const char PORTAL_SEC_LAMP[] PROGMEM =
    "<h3>This lamp</h3>";
static const char PORTAL_SEC_LOCK[] PROGMEM =
    "<h3>Lock the web page</h3>"
    "<p class=\"hint\">Leave blank on a home network. Set a PIN if the lamp shares "
    "Wi-Fi with people you would rather not have reading the love notes.</p>";
static const char PORTAL_SEC_SYNC[] PROGMEM =
    "<h3>Sync server</h3>"
    "<p class=\"hint\">Your Firebase database URL and Web API key - see "
    "firebase/README.md. There is no shared default; every couple runs their "
    "own free project.</p>";

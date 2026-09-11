# Ultimate Romantic Couple Lamp

Two lamps, one pair. Touch yours, theirs buzzes. Change your colour, theirs
follows. On an anniversary both light up, sing and pulse a heartbeat.

Three parts, one Firebase project tying them together:

```
esp32-lovelamp/
  esp32-lovelamp.ino       ESP32 firmware - Arduino IDE sketch (this folder)
  Config.h                 pinout, defaults, version, GitHub OTA URLs
  OTA.h                    checkGitHubUpdate() / performOTAUpdate()
  FirebaseSync.h           background task: device provisioning + lamp_sync stream
  WebPage.h                the lamp's own 5-tab responsive UI, in PROGMEM
  PortalPage.h             skin for the Wi-Fi setup portal
  firmware/version.json    publish this on GitHub for OTA
  firebase/                database.rules.json, firebase.json, setup guide
  webapp/                  Next.js (static export) account/pairing/dashboard - for Cloudflare Pages
```

The firmware never asks anyone to type a Pair ID. A lamp knows only its own
MAC address; **pairing itself happens in `webapp/`**, the Love Lamp web app -
sign up, register a lamp by the MAC address its OLED shows, then create or
join a pair. See `webapp/README.md` for that half, and *How pairing works*
below for how the three parts fit together.

## Arduino IDE setup

**Boards manager** — add this URL in *File → Preferences → Additional boards
manager URLs*, then install **esp32 by Espressif Systems**. Version **2.0.17**
is what this sketch was originally sized against (see *Build status*):

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

**Tools menu**

| Setting | Value |
|---|---|
| Board | ESP32 Dev Module |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA)** - the Firebase client library is sizeable; don't try the Default 1.2 MB scheme without checking the compiled percentage first (see *Build status*) |
| Flash Size | 4MB (32Mb) |
| Upload Speed | 921600 |

**Libraries** (*Sketch → Include Library → Manage Libraries…*)

- **Firebase ESP Client** by mobizt — install the latest 4.4.x release. This
  is the library GitHub hosts as `mobizt/Firebase-ESP-Client` (header
  `Firebase_ESP_Client.h`) - sometimes referred to by the name of its older,
  simpler ESP32-only predecessor, "Firebase-ESP32". `FirebaseSync.h` uses its
  email/password Auth flow and its `beginStream()` / `setStreamCallback()`
  realtime API - see that file's own header comment for why, and the
  *Unverified* note in *Build status* below.
- `Adafruit NeoPixel`
- `Adafruit SSD1306` + `Adafruit GFX Library` (pulled in as a dependency)
- `DFRobotDFPlayerMini`
- `ArduinoJson` by Benoit Blanchon — **v7.x**, the code uses the v7 API
- `WiFiManager` by tzapu — install **v2.0.17** from Library Manager, or
  *Sketch → Include Library → Add .ZIP Library…* with
  <https://github.com/tzapu/WiFiManager/archive/refs/tags/v2.0.17.zip>

WiFi, WebServer, ESPmDNS and Preferences ship with the ESP32 core — nothing
to install for those. `HTTPClient`/`WiFiClientSecure` are still used by
`OTA.h` for GitHub firmware updates, independent of the Firebase library.

### Build status

**Unverified against a real compile.** This environment has no `arduino-cli`
and no way to install a third-party Arduino library, so `FirebaseSync.h` -
the file that actually calls into `Firebase_ESP_Client` - has been written
against that library's most stable, widely-documented API (email/password
sign-up/sign-in via `auth.user.email`/`auth.user.password`, then
`Firebase.begin()`; `Firebase.RTDB.beginStream()` +
`Firebase.RTDB.setStreamCallback()` for the realtime listener;
`Firebase.RTDB.getJSON()` / `setJSON()` / `updateNode()` for one-off reads
and writes) but has not been built or flashed. **Before flashing real
hardware**: open the library's own
`examples/RTDB/Stream/StreamCallback/StreamCallback.ino` and
`examples/Authentication/SignInAsUser/SignInAsUser.ino` and confirm the
method signatures there match what's installed (mobizt has shipped breaking
API changes across major versions before), then do a full Verify/compile.

The earlier MQTT-era build of this sketch measured 89% of the Default
(1.2 MB) partition on core 2.0.17 (see git history). `Firebase_ESP_Client`
pulls in its own JSON and TLS-adjacent machinery on top of what OTA already
needed, and is a meaningfully larger dependency than the plain
`HTTPClient`-based transports earlier revisions of this firmware used -
**Minimal SPIFFS is the safe starting assumption**, not Default. Watch the
compiled percentage and drop back to Default only if it's comfortably clear.

Untested on real hardware — nothing here has been flashed to a board yet.

## Wiring

| Peripheral | ESP32 pin | Notes |
|---|---|---|
| WS2812B DIN | GPIO 15 | 330–470 Ω in series; 1000 µF across 5 V/GND |
| TTP223 OUT | GPIO 4 | active HIGH |
| OLED SDA / SCL | GPIO 21 / 22 | SSD1306 or SH1106 @ 0x3C |
| Vibration motor | GPIO 13 | via NPN/MOSFET + flyback diode — never straight off the GPIO |
| DFPlayer TX → ESP32 RX2 | GPIO 16 | |
| ESP32 TX2 → DFPlayer RX | GPIO 17 | 1 kΩ in series (DFPlayer RX is 3.3 V-ish) |

Power the LED strip and DFPlayer from a 5 V supply, not the ESP32's regulator,
and tie all grounds together. GPIO 16/17 are free on WROOM modules; on a
**WROVER** they belong to the PSRAM — move Serial2 to other pins in `Config.h`.

SD card for the DFPlayer: `/mp3/0001.mp3` chime, `0002.mp3` lullaby,
`0003.mp3` celebration.

## Backend setup (Firebase)

Both the firmware and the web app talk to the same Firebase project instead
of an MQTT broker or any server you'd have to run yourself. One-time setup,
before flashing anything - see `firebase/README.md` for the full walkthrough:

1. Create a project at [console.firebase.google.com](https://console.firebase.google.com),
   add a Realtime Database, and enable **both** the **Phone** and
   **Email/Password** sign-in providers under Authentication - humans sign
   in with the former (OTP), lamps with the latter (a synthetic,
   MAC-derived identity - see *How pairing works* below).
2. Deploy `firebase/database.rules.json` (Firebase CLI, or paste it into
   the console's Rules editor and Publish).
3. Register a **Web app** in Project settings to get the config values
   (`apiKey`, `authDomain`, `databaseURL`, `projectId`, `appId`) - the web
   app needs all five; the firmware only needs the API key and database URL.
4. Set up `webapp/` against the same project - see `webapp/README.md`. You'll
   want it running (locally or deployed) before pairing a lamp, since that's
   where pairing actually happens.

## First boot

1. Upload the sketch, then open Serial Monitor at **115200**.
2. The lamp opens the AP **`LoveLamp_Setup`** (password `loveyou123`).
3. In the captive portal pick your Wi-Fi, name the lamp, and paste in the
   Firebase database URL and Web API key from above. There is no Pair ID
   field - pairing happens afterwards, in the web app.
4. Once connected, the lamp self-registers under its own MAC address and its
   OLED shows an onboarding screen with that MAC. Reach its own UI at
   `http://<device name>.local` (or the IP from the serial log) if you want
   to check on it from a browser too.
5. In the web app: sign in, register the lamp by pasting in the MAC address
   from its OLED, then either **start a pair** (you'll get a code) or **join
   a pair** (enter your partner's code). The lamp picks this up within a few
   seconds and its OLED switches from onboarding to normal operation.
6. Repeat for the second lamp - register it under a *different* user account
   (or add it to yours if you're setting up both sides yourself), then join
   the same pair with the code from step 5.

Timezone defaults to `ICT-7` (Asia/Bangkok) in `Config.h`. Time zone is
per-lamp — set it in the Setup tab, so two lamps in different countries each
fire their anniversaries on their own local date.

**Locking the page.** With no PIN set, anyone on the Wi-Fi can open the lamp and
read the notes. Set one in the portal or under Setup → Lock this page; the
browser then asks for username `lamp` and that PIN. It is HTTP basic auth over
plain HTTP: it stops housemates, not someone with a packet sniffer. Forget the
PIN and the only way back in is a serial re-flash with NVS erased.

## How pairing works

Two Firebase Auth providers, and four Realtime Database nodes, working
together. See `firebase/README.md` for the full tree shape and the *Why two
different Auth providers* section this summarizes - this is the trust model
behind it.

Every caller is one of exactly two kinds, authenticated completely
differently: a **human**, in the web app, who signs in with their **phone
number** (`signInWithPhoneNumber` + a 6-digit code - see `webapp/app/login/page.tsx`,
no password, no separate sign-up step), or a **lamp**, which - obviously
unable to receive an SMS - signs in as a synthetic **email/password**
identity it invents for itself (below). The Realtime Database rules tell
the two apart with a single claim on the ID token, `auth.token.phone_number`,
present only on a genuine phone sign-in: every rule that means "a human who
owns or partners this pair" checks it explicitly, and every rule that means
"the device itself" doesn't - so neither can ever impersonate the other.

- **`/devices/{mac}`** - one node per physical lamp, keyed by its MAC address
  (12 uppercase hex characters, no colons). A lamp mints its own
  `deviceSecret` (48 random hex characters from `esp_random()` + the eFuse
  MAC, in `mintDeviceSecret()`) the first time it ever boots, derives a fixed
  email from its own MAC, and signs up for a Firebase Auth account with that
  email/secret pair - the *same* stable `auth.uid` comes back on every future
  boot, since it's a real sign-*in* from then on. Realtime Database rules
  scope that uid to exactly this one node, nothing else.
- **`/userDevices/{uid}/{mac}`** - an index, not a table: Realtime Database
  rules can't filter a list query per-row the way Postgres RLS could, so
  "which devices does this account own" has to be a real, directly-readable
  node the web app writes to at claim time, not a filtered scan over
  `/devices`.
- **`/pairs/{pairId}`** - one node per couple, created the moment someone
  clicks "start a new pair" in the web app. It gets a random 8-character
  `pairCode` (indexed at `/pairCodes/{code}` for the join flow's lookup) to
  hand to a partner, and denormalized copies of both device names and both
  devices' own `authUid`s (see the next paragraph) so a dashboard or a lamp
  never needs read access to a stranger's `/devices` node just to know who
  they're paired with.
- **`/lamp_sync/{pairId}/{role}`** - one node per *paired* device
  (`role` is `"A"` or `"B"`), holding the live state (`power`, `color`,
  `bright`, `mood`, `note`, `vib`, `track`, `alert`) that both lamps and the
  web dashboard read and write. This is exactly the path shape the firmware
  streams: `/lamp_sync/{pair_id}/{partner_role}`.

**Claiming.** A freshly self-registered device has no `userId`. The web
app's Device Registration form does exactly one multi-path update: set
`devices/{mac}/userId` to the signed-in user and add `userDevices/{uid}/{mac}`.
That's it - the firmware never proves it owns the MAC beyond having phoned
home with it, the same trust level a MAC address has always had (an
identifier, not a secret). What *is* a secret is `deviceSecret`, and the web
app never touches it - it only ever exists as a Firebase Auth password,
never written into the database itself.

**Pairing.** Realtime Database has no server-side stored-procedure
equivalent (that needs a Cloud Function, out of scope for a 3-part,
no-backend-server project), so create/join/leave are expressed as **atomic
multi-path `update()` calls from the web app**, authorized entirely by
`firebase/database.rules.json`. Firebase evaluates every path in one such
call against the *resulting* tree together - either the whole update commits
or none of it does - which is what makes "create a pair, mint its code, and
assign my device to role A" (three different nodes) safe without a database
transaction in the SQL sense. A concurrent join race is settled by a rule
requiring `pairs/{pairId}/partnerId`'s *old* value to be empty: whichever
`update()` call actually lands first wins, and the second is rejected at
commit time by Realtime Database's own serialization, not by anything the
client does.

Each device claims its own `pairs/{pairId}/deviceAuthA` (or B) slot itself,
right after discovering it's been paired (`claimPairSlot()` in
`FirebaseSync.h`) - a one-time, self-only write (see the rules) - which is
what lets the *lamp itself*, not just the human's browser session, read and
write its own `lamp_sync` leaf and stream the partner's. It checks the slot
before writing it: the rule only permits the write while the slot is still
empty, so a lamp that reboots after already having claimed it reads back its
own uid and stops there instead of retrying a write that would now be
rejected.

**Sync itself is push, not poll.** `FirebaseSync.h` runs its own FreeRTOS
task on core 0 (so `loop()` never blocks on it) and keeps a live Realtime
Database *stream* open on the partner's `lamp_sync` leaf - a change arrives
the instant Firebase delivers it, no HTTP polling loop involved. It upserts
its own leaf on every real change, and periodically with no changes
(`FIREBASE_HEARTBEAT_MS`, triggered from the `.ino`'s `firebaseTask()`, which
is also what reads the stream's incoming mailbox) so the leaf doesn't look
stale to the partner while idle. A role clash simply can't happen: the rules
only ever let a device or its owning user write role `A`'s leaf if they're
literally the pair's `ownerId`/`deviceAuthA`, same for `B` - there's no path
by which two devices could both claim the same role.

There is no retained flag and no last-will - nothing is watching the
connection for us. "Partner linked" means only that we heard from their leaf
inside the last `PARTNER_STALE_MS` (`Config.h`), whether because they
changed something or because their own periodic heartbeat landed. A lamp
that loses power simply stops refreshing its leaf and goes stale after that
window - and its Realtime Database stream connection drops out from under
it, which the *partner's* stream doesn't observe directly (staleness is
still the only presence signal, exactly as before).

**Losing the device secret.** Re-flashing after an NVS wipe mints a new
secret, which means a new email/password pair that Firebase Auth has never
seen before *for that email* - except the email is only MAC-derived, so it's
the *same* email as before, now with the wrong password. Both sign-in and
sign-up fail (the account already exists under the old secret), which the
firmware surfaces, best-effort, as "can't sign in" on the OLED after a few
failed attempts (`hasAuthTrouble()` in `FirebaseSync.h`) - a softer signal
than the Supabase-backed version's crisp HTTP 409, since this library's
async auth state doesn't cleanly distinguish "wrong secret" from "still
connecting" or "Firebase is briefly unreachable". Delete the stale
`/devices/{mac}` node from the Firebase console (or `unregisterDevice` it
from the web app first) to free the MAC up again.

## Touch

- **Tap** — chime + heartbeat buzz on the partner's lamp.
- **Hold ≥ 1.2 s** — toggle this lamp's light (synced).

## Anniversaries

Tab 5 stores up to 12 `{name, day, month}` entries in NVS as JSON. Once the day
rolls over (NTP time required) a DD/MM match fires the alert for 60 s: scrolling
OLED message, track 3, heartbeat vibration, rainbow↔pink pulse — and it is
pushed to the partner's lamp so both celebrate together.

## OTA from GitHub

1. Bump `FW_VERSION` in `Config.h`, set `OTA_GITHUB_USER` / `OTA_GITHUB_REPO`.
2. *Sketch → Export Compiled Binary* (`Ctrl+Alt+S`) — it drops
   `build/esp32.esp32.esp32/esp32-lovelamp.ino.bin` in the sketch folder.
   Upload that to a GitHub release as `firmware.bin`.
3. Update `firmware/version.json` on the branch with the new `version` + `url`
   and push it.
4. On the lamp: Settings tab → **Check update** → **Update now**.

### Updating a pair

An OTA writes only the spare app partition, so **NVS survives**: the device
secret, Wi-Fi credentials, special dates and the last love note are all still
there when the lamp comes back up. Pairing itself lives in Firebase, keyed to
that surviving device secret (by way of the stable Firebase Auth uid it
signs in as), so nothing needs re-pairing either.

You update one lamp at a time — each checks GitHub and flashes itself — so the
pair *will* run mismatched versions for a while. Both lamps read and write
the same fixed fields of the same shared `lamp_sync` shape, so any firmware
built against `firebase/database.rules.json` interoperates with any other.
The thing to keep backward-compatible is the **rules and data shape**, not
the firmware — if you add a field, give it a sensible default in the rules
and don't rename or drop one a lamp still in the field reads or writes.

Two things that are *not* safe over OTA:

- **A binary that outgrew the partition.** See *Build status* above - this
  is a real risk with a library the size of `Firebase_ESP_Client` aboard.
  Watch the percentage after every compile.
- **Changing the partition scheme.** That rewrites the flash layout, which an
  OTA cannot do. Both lamps need a USB cable for that one.

Also avoid *Erase All Flash Before Sketch Upload* on a serial upload: it wipes
NVS, and the lamp mints a brand-new device secret - which means a brand-new,
unpaired identity as far as Firebase is concerned (see *Losing the device
secret* above).

`OTA.h` fetches `version.json` over HTTPS with `client.setInsecure()` — the
transfer is encrypted but the server certificate is not verified. Pin GitHub's
root CA there if you want that guarantee.

## Notes on the design

`loop()` never calls `delay()` — LEDs, vibration, OLED scrolling, touch
debounce and the daily anniversary check are each a `millis()` state machine.
The two deliberate exceptions are boot-time (WiFiManager's captive portal)
and the OTA flash itself, which is a user-initiated, device-blocking
operation by nature. Firebase sync would have been a third: Auth sign-in and
Realtime Database calls each block for a meaningful fraction of a second,
which is exactly the kind of stall `loop()` is designed never to have.
Instead `FirebaseSync.h` runs its own FreeRTOS task on core 0 and hands
state to and from `loop()` through a mutex-guarded mailbox, so none of that
ever touches the core `loop()` runs on - the realtime stream callback itself
fires on the Firebase library's own background task, one layer further out
still.

Lamp state is written back to NVS at most once every 3 s so dragging the colour
picker doesn't burn out the flash.

No function in the `.ino` uses default arguments: the Arduino IDE
auto-generates prototypes for sketch files and default values there collide
with the definition. `publishState()` and `drawLine()` are plain overloads for
that reason — keep it that way when you extend the sketch.

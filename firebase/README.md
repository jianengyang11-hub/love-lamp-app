# Firebase backend setup

Realtime Database + Auth, replacing the Supabase Postgres/RLS backend. See
the root `README.md`'s *How pairing works* section for the full picture and
`database.rules.json` in this folder for the actual rules.

## 1. Create the project

1. [console.firebase.google.com](https://console.firebase.google.com) → **Add project**.
2. **Build → Realtime Database → Create database.** Pick a location, start in
   **locked mode** (rules deployed below immediately replace the default
   "deny all" with the real ruleset, so locked-mode's default is fine as a
   starting point). Note the **database URL** shown
   (`https://<project>-default-rtdb.<region>.firebasedatabase.app`) - both
   the web app and the firmware need it.
3. **Build → Authentication → Get started → Sign-in method**, and enable
   **both** of these providers - this project uses two, deliberately, one
   per kind of caller (see *Why two different Auth providers* below):
   - **Phone** - what every human signs in with, in the web app. Toggle it
     on; no extra configuration is required for basic use, though you'll
     want to add a couple of **Phone numbers for testing** (same screen) so
     you can develop without burning real SMS sends - e.g. `+15555550100`
     with a fixed code like `123456`.
   - **Email/Password** - what every *lamp* signs in as (a synthetic,
     MAC-derived identity - no human ever sees or uses this provider
     directly). Toggle it on too; nothing else to configure.
4. Phone sign-in needs reCAPTCHA, which needs your web app's origin on the
   allow list: **Authentication → Settings → Authorized domains** - add
   `localhost` (present by default) and, once you have one, your Cloudflare
   Pages URL.
5. **Project settings (gear icon) → General → Your apps → Add app → Web
   (`</>`).** Register an app (no Firebase Hosting needed - this project
   deploys to Cloudflare Pages instead). Copy the resulting config object -
   `apiKey`, `authDomain`, `databaseURL`, `projectId`, `appId` - into
   `webapp/.env.local` (see `webapp/README.md`).
6. The same `apiKey` and `databaseURL` also go into the ESP32's setup portal
   (`Config.h` / `PortalPage.h` - see the firmware section of the root
   README). Firebase Web API keys are meant to be embedded in clients (web
   *and* device firmware both) - they identify the project, they don't
   grant access on their own. Access control is entirely the Realtime
   Database rules below plus Firebase Auth.

**A note on cost.** Phone Auth's free tier covers a modest number of SMS
verifications a month; past that (or in some regions from the first message)
it bills per verification, and requires the project to be on the pay-as-you-go
**Blaze** plan even to send real SMS at all - the **Spark** (free) plan can
still develop entirely against the test phone numbers from step 3. Realtime
Database itself has its own separate free tier, generous enough for a
handful of lamps.

## 2. Deploy the rules

Install the Firebase CLI once (`npm install -g firebase-tools`), then from
this `firebase/` folder:

```
firebase login
firebase use --add          # pick your project, give it an alias (e.g. "default")
firebase deploy --only database
```

That needs a `firebase.json` pointing at `database.rules.json` - if you
haven't run `firebase init database` in this folder yet, create one:

```json
{
  "database": {
    "rules": "database.rules.json"
  }
}
```

Alternatively, paste `database.rules.json`'s contents directly into
**Realtime Database → Rules** in the console and click **Publish** - no CLI
needed for a one-off deploy.

**Test before you trust it**: `firebase emulators:start --only database,auth`
runs the Realtime Database and Auth emulators locally so you can exercise
the pairing flow against these exact rules without touching production data.
RTDB's rule language has no unit-test runner built into this repo - the
emulator is the closest thing to one.

## 3. Data shape

```
/devices/{MAC}                    MAC = 12 uppercase hex chars, no colons (AABBCCDDEEFF)
  mac: "AABBCCDDEEFF"
  authUid: "<firebase auth uid this device signs in as>"
  userId: null | "<uid of the user who claimed it>"
  pairId: null | "<pairs push id>"
  role: null | "A" | "B"
  name: "My Lamp"
  createdAt / lastSeenAt: server timestamps

/userDevices/{uid}/{mac}: true    index so a user's own devices can be listed at all - Realtime
                                   Database rules can't filter a query per-row the way Postgres RLS
                                   does, so "which devices are mine" has to be a real, denormalized,
                                   directly-readable node rather than a filtered query over /devices

/pairs/{pairId}                   pairId = Realtime Database push() id
  pairCode: "XXXXXXXX"             8 chars, client-generated, indexed below
  ownerId: "<uid>"                 created the pair, is role A
  partnerId: null | "<uid>"        joined via pairCode, is role B
  deviceAuthA / deviceAuthB: null | "<device's own authUid>"   self-claimed once by each device after pairing, see below
  deviceNameA / deviceNameB: null | "My Lamp"                  denormalized so the dashboard can show a partner's lamp name without needing read access to their /devices node
  createdAt / updatedAt: server timestamps

/pairCodes/{pairCode}             index: pairCode -> pairId, for the join flow's lookup
  (value is just the pairId string)

/lamp_sync/{pairId}/{role}        role = "A" | "B" - exactly the path shape
                                   the firmware streams: /lamp_sync/{pair_id}/{partner_role}
  deviceId: "<mac>"
  power, color, bright, mood, note, vib, track, alert
  updatedAt: server timestamp
```

## Why two different Auth providers

Every caller against this database is one of exactly two kinds - a human,
in the web app, or a lamp - and they authenticate completely differently.

**Humans use Phone (OTP).** `signInWithPhoneNumber()` + a 6-digit code
confirmed client-side (`webapp/app/login/page.tsx`) - no password to manage,
no email confirmation link to click. Firebase creates the account
automatically the first time a number verifies successfully; every later
sign-in with the same number resolves to the same `uid`.

**Lamps use Email/Password, under an identity they invent for themselves.**
A lamp obviously can't receive an SMS, so Phone auth was never an option for
it - and Realtime Database rules can only ever check `auth.uid` (and custom
claims, which need a Cloud Function to mint, out of scope for a 3-part,
no-backend-server project). Two options exist for giving a device its own
stable `auth.uid` without one:

- **Anonymous auth** - simple, but a *new* anonymous UID is minted every
  session unless you carefully persist and restore the exact same session,
  which this library ecosystem doesn't make straightforward. A device whose
  UID changes on every reboot can't be recognised as "the same device" by
  rules that pinned `authUid` to it at registration time.
- **Email/Password auth with a device-derived identity** - what this project
  uses. Each lamp mints a random `deviceSecret` once (48 hex characters,
  `mintDeviceSecret()` in the `.ino`, persisted in NVS - exactly the same
  pattern the Supabase version of this firmware used for its
  `x-device-secret` header) and derives a fixed, deterministic email from
  its own MAC address: `device-<mac>@lovelamp.device`. It signs up **once**
  (first boot) with that email and `deviceSecret` as the password, then
  every later boot just **signs in** with the same email/password - Firebase
  Auth gives it back the *same* `uid` every time, indefinitely, with no
  session-restoration trickery needed. Losing `deviceSecret` (an NVS wipe)
  means the device can no longer sign in under the email its old `devices`
  row remembers, which surfaces as a "can't sign in" state on its OLED - see
  the firmware's `FirebaseSync.h` and `hasAuthTrouble()`.

**The rules tell the two apart with one claim**: `auth.token.phone_number`
exists only on a session that actually came from the Phone provider - never
on a device's email/password session. Every rule branch that means "a human
who owns or partners this pair" checks that claim explicitly (see
`database.rules.json`); every branch that means "the device itself" checks
`authUid` / `deviceAuthA` / `deviceAuthB` instead and doesn't care about
phone status at all. A lamp's own Firebase session can never satisfy a
human-only rule, and a phone session can never impersonate a lamp.

Either way, an account is exactly as sensitive as the credential that
produced it: whoever holds a `deviceSecret` can authenticate as that one
specific lamp and nothing else (Realtime Database rules never grant a
device any access beyond its own `devices/{mac}` row and the two
`lamp_sync` leaves of whatever pair it belongs to); whoever can receive a
verification SMS at a given number can sign in as that number's account.

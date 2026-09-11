# Love Lamp web app

Next.js (App Router), statically exported for Cloudflare Pages - account,
device registration, pairing and a live dashboard for the ESP32 Love Lamp.
Talks to the same Firebase project as the firmware - see
`../firebase/README.md` and the root `README.md`'s *How pairing works*
section for the full picture of how the three parts fit together.

## Why static export

Cloudflare Pages, in the plain "static site" mode this app targets, serves
files - there is no Node.js server behind it, and the Firebase Web SDK
doesn't need one: it's a browser SDK by design. `next.config.js` sets
`output: "export"`, and every page here is a Client Component
(`"use client"`) that calls Firebase directly via `lib/firebase/client.ts`.
Every mutation (register a device, pair, control the lamp) lives in
`lib/actions.ts` as a plain async function. Access control is entirely
Firebase Auth + the Realtime Database security rules in
`../firebase/database.rules.json` - not which machine issued the request.

## Setup

1. Follow `../firebase/README.md` to create the Firebase project, enable
   **both** the Phone and Email/Password sign-in providers (one for humans,
   one for lamps - see that file's *Why two different Auth providers*), and
   deploy the Realtime Database rules. Add a couple of **test phone
   numbers** there too so you can develop this app without spending real SMS.
2. `cp .env.local.example .env.local` and fill in the five `NEXT_PUBLIC_FIREBASE_*`
   values from Firebase console → Project settings → General → Your apps →
   the web app you registered.
3. `npm install`
4. `npm run dev` - open <http://localhost:3000>.

## What's here

```
next.config.js              output: "export" + images.unoptimized
app/
  login/page.tsx              phone number + OTP sign-in (signInWithPhoneNumber +
                               RecaptchaVerifier), calling firebase/auth directly -
                               the only auth page; there's no separate sign-up
  dashboard/page.tsx          the whole authed experience: client-side auth guard,
                               loads /userDevices + /devices + /pairs, renders per device
components/
  DeviceRegisterForm.tsx      bind a lamp to your account by MAC address
  DeviceSettings.tsx          rename/remove a device, online dot, last-seen
  PairingPanel.tsx            create a pair (get a code) or join one (enter a code)
  LampDashboard.tsx           colour/brightness/mood/notes + partner status,
                               live via Firebase Realtime Database onValue() listeners
  SignOutButton.tsx           client-side firebase/auth signOut()
lib/
  firebase/client.ts          the one Firebase app/auth/database instance in the app
  actions.ts                  every mutation - register/rename/unregister a
                               device, create/join/leave a pair, lamp control
  types.ts                    row shapes matching firebase/database.rules.json exactly
```

## Phone sign-in, specifically

`app/login/page.tsx` is the whole auth surface - two steps, no separate
sign-up route:

1. **Enter a phone number.** An invisible `RecaptchaVerifier` (constructed
   once, into a hidden `<div>` on the page - required by Firebase even in
   invisible mode) backs the `signInWithPhoneNumber()` call. A failed send
   resets the widget so the next attempt gets a fresh challenge, per
   Firebase's own guidance.
2. **Enter the 6-digit code** texted to that number, confirmed with the
   `ConfirmationResult` from step 1. Firebase creates the account
   automatically the first time a number ever verifies successfully, and
   signs the same account back in on every later visit - there's nothing
   that distinguishes "new user" from "returning user" at this layer, so
   there's nothing here that needs its own sign-up form or a
   confirmation-link callback page (unlike email-based auth).

Local development: use one of the **test phone numbers** configured in the
Firebase console (see `../firebase/README.md`) with its fixed code - no SMS
actually sends, and reCAPTCHA is bypassed for those numbers.

## Why there's no broad "list my devices" query

Realtime Database security rules can't filter a list query per-row the way
Postgres RLS could - a rule granting read access at `/devices/{mac}` doesn't
let a client query the whole `/devices` collection and have results silently
filtered to what they're allowed to see. Firebase's standard answer is
denormalization: `/userDevices/{uid}/{mac}` is a real, directly-readable
index the app writes to whenever a device is claimed, and `dashboard/page.tsx`
reads that first to find out *which* MACs to look up individually. See
`../firebase/README.md`'s data-shape section for the rest of the tree.

## How the dashboard writes reach the lamp

Sending a note or dragging the brightness slider writes straight to
`/lamp_sync/{pairId}/{yourRole}` (`updateLampState` in `lib/actions.ts`) -
exactly what the lamp's own touch sensor or its local Web UI would do. The
physical lamp picks it up **instantly** via the Realtime Database stream it
holds open on `/lamp_sync/{pairId}/{partnerRole}` (see the firmware's
`FirebaseSync.h`) - no polling on either side. The dashboard itself uses the
same mechanism in reverse: `LampDashboard.tsx` calls `onValue()` on both
lamp_sync leaves of the pair, so a change from the partner's physical lamp
(or their dashboard) appears the moment Firebase delivers it. Structural
changes (registering, pairing, unpairing) call the `onChanged` callback each
component receives, which re-fetches the device/pair list - there's no
server cache to invalidate here, just local React state.

## Deploying to Cloudflare Pages

**Dashboard setup** (Pages → Create a project → Connect to Git):

| Setting | Value |
|---|---|
| Framework preset | Next.js (Static HTML Export) |
| Build command | `npm run build` |
| Build output directory | `out` |
| Root directory | `webapp` (if this repo's `webapp/` folder isn't the repo root) |

Add all five `NEXT_PUBLIC_FIREBASE_*` variables under **Settings →
Environment variables** for both Production and Preview - these are
**build-time** variables baked into the JS bundle, not runtime ones, so they
must be set before the build runs, and changing them requires a re-deploy.

**CLI alternative**, from `webapp/`:

```
npm run build
npx wrangler pages deploy out --project-name=love-lamp
```

Either way, once you have the deployed URL, add it to Firebase Auth's
authorized domains (**Authentication → Settings → Authorized domains** in
the Firebase console) - sign-in silently fails from an unlisted origin.

**Local preview of the static build** (closer to what Cloudflare actually
serves than `next dev`): `npm run build && npm run preview`.

import { ref, get, update, push, serverTimestamp } from "firebase/database";
import { getFirebaseAuth, getFirebaseDb } from "@/lib/firebase/client";
import type { Mood, Pair } from "@/lib/types";
import { DEFAULT_LAMP_SYNC } from "@/lib/types";

/* =============================================================================
 *  lib/actions.ts - every mutation the app makes, all running client-side
 *  against Firebase directly (there is no server in a static export - see
 *  next.config.js). Every one of these is exactly as safe as it looks:
 *  Realtime Database security rules (see ../firebase/database.rules.json)
 *  are the actual access-control boundary, evaluated atomically across every
 *  path touched by a single `update()` call - that atomicity is what makes
 *  "pairing" (several nodes changing together) safe without a server-side
 *  transaction of the kind a SQL backend would use.
 * ========================================================================== */

export type ActionResult = { error: string | null };

const PAIR_CODE_ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"; // matches firmware/rules - no 0/1/I/O/L

function generatePairCode(): string {
  let code = "";
  const bytes = new Uint8Array(8);
  crypto.getRandomValues(bytes);
  for (const b of bytes) code += PAIR_CODE_ALPHABET[b % PAIR_CODE_ALPHABET.length];
  return code;
}

/** Accepts "aabbccddeeff", "aa-bb-cc-dd-ee-ff" or "AA:BB:CC:DD:EE:FF" and
 *  normalises to the 12-uppercase-hex-character form used as the Realtime
 *  Database key (RTDB keys can't contain some punctuation, and a bare hex
 *  string sidesteps the question entirely). */
function normalizeMac(raw: string): string | null {
  const hex = raw.replace(/[^0-9a-fA-F]/g, "").toUpperCase();
  return hex.length === 12 ? hex : null;
}

function requireUid(): string {
  const uid = getFirebaseAuth().currentUser?.uid;
  if (!uid) throw new Error("not signed in");
  return uid;
}

function friendlyError(err: unknown): string {
  if (err instanceof Error) {
    if (err.message.includes("PERMISSION_DENIED")) {
      return "That wasn't allowed - the lamp or pair may have changed since this page loaded. Try refreshing.";
    }
    return err.message;
  }
  return "Something went wrong.";
}

/* --------------------------------------------------------------------------
 *  Device Registration - bind an ESP32 to the signed-in user by MAC address.
 *  The device must have already self-registered an unclaimed row (it does
 *  this on its own the first time it comes online with Wi-Fi and Firebase
 *  credentials configured) - this only attaches it to an account.
 * ----------------------------------------------------------------------- */
export async function registerDevice(macInput: string): Promise<ActionResult> {
  const mac = normalizeMac(macInput);
  if (!mac) {
    return { error: "That doesn't look like a MAC address (expected 12 hex characters)." };
  }

  try {
    const uid = requireUid();
    const db = getFirebaseDb();

    const snap = await get(ref(db, `devices/${mac}`));
    if (!snap.exists()) {
      return {
        error:
          "No lamp found with that MAC address yet. Make sure it's powered on and connected to Wi-Fi " +
          "- it takes a few seconds to phone home the first time.",
      };
    }
    if (snap.val().userId) {
      return { error: "That lamp is already claimed by an account." };
    }

    await update(ref(db), {
      [`/devices/${mac}/userId`]: uid,
      [`/userDevices/${uid}/${mac}`]: true,
    });
    return { error: null };
  } catch (err) {
    return { error: friendlyError(err) };
  }
}

export async function renameDevice(mac: string, name: string): Promise<ActionResult> {
  const trimmed = name.trim().slice(0, 40);
  if (!trimmed) return { error: "Name can't be empty." };

  try {
    const db = getFirebaseDb();
    const updates: Record<string, unknown> = { [`/devices/${mac}/name`]: trimmed };

    // Keep pairs/{pairId}/deviceName{A,B} in sync - that denormalized copy
    // is what lets a partner's dashboard show this lamp's name without
    // needing read access to someone else's /devices node (see
    // ../firebase/database.rules.json).
    const snap = await get(ref(db, `devices/${mac}`));
    const device = snap.val();
    if (device?.pairId && device?.role) {
      updates[`/pairs/${device.pairId}/deviceName${device.role}`] = trimmed;
    }

    await update(ref(db), updates);
    return { error: null };
  } catch (err) {
    return { error: friendlyError(err) };
  }
}

/**
 * Shared by unregisterDevice() and leavePair(): detaches a device from
 * whatever pair it's in, and - if the device was role A and no partner ever
 * joined - deletes the now-useless pair too.
 *
 * The pairCodes index entry has to be removed in its *own* update() call,
 * before the pair itself is deleted: the rule authorizing that delete reads
 * `pairs/{pairId}/ownerId` (see database.rules.json), and Realtime Database
 * evaluates every path in one multi-location update against the *resulting*
 * tree - if `/pairs/{pairId}` were deleted in the same batch, that ownerId
 * would already be gone by the time the rule looked for it. Two sequential
 * calls trade a moment of non-atomicity (a crash between them could leave an
 * orphaned, code-less, unpaired `/pairs/{pairId}` node) for a rule that's
 * actually satisfiable - and an orphan like that is inert: nothing points at
 * it, nobody can reach it, it isn't a security or data-integrity issue.
 *
 * Returns the update fragment for the caller's own multi-path update
 * (devices/userDevices cleanup + this), after already having removed the
 * pairCode index if applicable.
 */
async function buildLeavePairUpdates(
  mac: string,
  pairId: string,
  role: "A" | "B"
): Promise<Record<string, unknown>> {
  const db = getFirebaseDb();
  const updates: Record<string, unknown> = {
    [`/lamp_sync/${pairId}/${role}`]: null,
  };

  if (role === "A") {
    const pairSnap = await get(ref(db, `pairs/${pairId}`));
    const pair = pairSnap.val() as Pair | null;
    if (pair && !pair.partnerId) {
      await update(ref(db), { [`/pairCodes/${pair.pairCode}`]: null });
      updates[`/pairs/${pairId}`] = null;
    }
  }

  return updates;
}

export async function unregisterDevice(mac: string): Promise<ActionResult> {
  try {
    const uid = requireUid();
    const db = getFirebaseDb();

    const snap = await get(ref(db, `devices/${mac}`));
    const device = snap.val();
    if (!device) return { error: null }; // already gone

    const updates: Record<string, unknown> = {
      [`/devices/${mac}`]: null,
      [`/userDevices/${uid}/${mac}`]: null,
    };

    if (device.pairId && device.role) {
      Object.assign(updates, await buildLeavePairUpdates(mac, device.pairId, device.role));
    }

    await update(ref(db), updates);
    return { error: null };
  } catch (err) {
    return { error: friendlyError(err) };
  }
}

/* --------------------------------------------------------------------------
 *  Pairing workflow
 * ----------------------------------------------------------------------- */
export async function createPair(mac: string): Promise<ActionResult> {
  try {
    const uid = requireUid();
    const db = getFirebaseDb();

    const deviceSnap = await get(ref(db, `devices/${mac}`));
    const device = deviceSnap.val();
    if (!device || device.userId !== uid) return { error: "Device not found or not yours." };
    if (device.pairId) return { error: "This lamp is already paired." };

    const pairId = push(ref(db, "pairs")).key;
    if (!pairId) return { error: "Could not generate a pair - try again." };
    const pairCode = generatePairCode();

    const updates: Record<string, unknown> = {
      [`/pairs/${pairId}`]: {
        pairCode,
        ownerId: uid,
        deviceNameA: device.name ?? "My Lamp",
        createdAt: serverTimestamp(),
        updatedAt: serverTimestamp(),
      },
      [`/pairCodes/${pairCode}`]: pairId,
      [`/devices/${mac}/pairId`]: pairId,
      [`/devices/${mac}/role`]: "A",
      [`/lamp_sync/${pairId}/A`]: {
        ...DEFAULT_LAMP_SYNC,
        deviceId: mac,
        updatedAt: serverTimestamp(),
      },
    };

    await update(ref(db), updates);
    return { error: null };
  } catch (err) {
    return { error: friendlyError(err) };
  }
}

export async function joinPair(mac: string, pairCodeInput: string): Promise<ActionResult> {
  const pairCode = pairCodeInput.trim().toUpperCase();
  if (!pairCode) return { error: "Enter the pair code your partner shared with you." };

  try {
    const uid = requireUid();
    const db = getFirebaseDb();

    const deviceSnap = await get(ref(db, `devices/${mac}`));
    const device = deviceSnap.val();
    if (!device || device.userId !== uid) return { error: "Device not found or not yours." };
    if (device.pairId) return { error: "This lamp is already paired." };

    const codeSnap = await get(ref(db, `pairCodes/${pairCode}`));
    const pairId = codeSnap.val() as string | null;
    if (!pairId) return { error: "No pair found with that code." };

    const pairSnap = await get(ref(db, `pairs/${pairId}`));
    const pair = pairSnap.val() as Pair | null;
    if (!pair) return { error: "No pair found with that code." };
    if (pair.ownerId === uid) {
      return { error: "That's your own pair code - have your partner enter it on their account." };
    }
    if (pair.partnerId) return { error: "This pair already has two lamps." };

    const updates: Record<string, unknown> = {
      [`/pairs/${pairId}/partnerId`]: uid,
      [`/pairs/${pairId}/deviceNameB`]: device.name ?? "My Lamp",
      [`/pairs/${pairId}/updatedAt`]: serverTimestamp(),
      [`/devices/${mac}/pairId`]: pairId,
      [`/devices/${mac}/role`]: "B",
      [`/lamp_sync/${pairId}/B`]: {
        ...DEFAULT_LAMP_SYNC,
        deviceId: mac,
        updatedAt: serverTimestamp(),
      },
    };

    await update(ref(db), updates);
    return { error: null };
  } catch (err) {
    return { error: friendlyError(err) };
  }
}

export async function leavePair(mac: string): Promise<ActionResult> {
  try {
    const uid = requireUid();
    const db = getFirebaseDb();

    const snap = await get(ref(db, `devices/${mac}`));
    const device = snap.val();
    if (!device || device.userId !== uid) return { error: "Device not found or not yours." };
    if (!device.pairId || !device.role) return { error: "This lamp isn't paired." };

    const updates: Record<string, unknown> = {
      [`/devices/${mac}/pairId`]: null,
      [`/devices/${mac}/role`]: null,
      ...(await buildLeavePairUpdates(mac, device.pairId, device.role)),
    };

    await update(ref(db), updates);
    return { error: null };
  } catch (err) {
    return { error: friendlyError(err) };
  }
}

/* --------------------------------------------------------------------------
 *  Lamp control - the dashboard writes to its own device's lamp_sync leaf,
 *  exactly the way the physical lamp's touch sensor or its own local Web UI
 *  would. The partner's lamp (and their dashboard) picks it up the instant
 *  it arrives via a Realtime Database `onValue` listener (see
 *  components/LampDashboard.tsx), no polling involved on the web side.
 * ----------------------------------------------------------------------- */
export interface LampPatch {
  power?: boolean;
  color?: string;
  bright?: number;
  mood?: Mood;
  note?: string;
  vib?: boolean;
  track?: number;
  alert?: string;
}

const MOODS: Mood[] = ["normal", "miss", "sleep", "work", "hug"];

/**
 * Writes a full leaf snapshot every time, the same invariant the firmware's
 * own publishState() keeps (see FirebaseSync.h): "persistent" fields
 * (power, colour, brightness, mood, note) fall back to the leaf's current
 * value when this particular call doesn't touch them, but "pulse" fields
 * (vib, track, alert) always default to false/0/"" unless THIS call is the
 * event. If a buzz's `vib: true` were merely left out of the next unrelated
 * write instead of being explicitly re-asserted false, it would sit in the
 * node and misfire on the partner's lamp the next time anything else changed.
 */
export async function updateLampState(
  pairId: string,
  role: "A" | "B",
  patch: LampPatch
): Promise<ActionResult> {
  try {
    const db = getFirebaseDb();
    const snap = await get(ref(db, `lamp_sync/${pairId}/${role}`));
    const current = snap.val() ?? DEFAULT_LAMP_SYNC;

    const power = typeof patch.power === "boolean" ? patch.power : current.power;
    const color =
      typeof patch.color === "string" && /^#[0-9a-fA-F]{6}$/.test(patch.color) ? patch.color : current.color;
    const bright =
      typeof patch.bright === "number" ? Math.max(5, Math.min(255, Math.round(patch.bright))) : current.bright;
    const mood = typeof patch.mood === "string" && MOODS.includes(patch.mood) ? patch.mood : current.mood;
    const note = typeof patch.note === "string" ? patch.note.slice(0, 96) : current.note;

    const vib = typeof patch.vib === "boolean" ? patch.vib : false;
    const track = typeof patch.track === "number" ? Math.max(0, Math.min(255, Math.round(patch.track))) : 0;
    const alert = typeof patch.alert === "string" ? patch.alert.slice(0, 64) : "";

    await update(ref(db, `lamp_sync/${pairId}/${role}`), {
      power,
      color,
      bright,
      mood,
      note,
      vib,
      track,
      alert,
      updatedAt: serverTimestamp(),
    });
    return { error: null };
  } catch (err) {
    return { error: friendlyError(err) };
  }
}

/** One-shot "buzz" - a vibration pulse with no other state change, mirroring
 *  the lamp's own "Send a buzz" button. */
export async function sendBuzz(pairId: string, role: "A" | "B"): Promise<ActionResult> {
  return updateLampState(pairId, role, { vib: true, track: 1 });
}

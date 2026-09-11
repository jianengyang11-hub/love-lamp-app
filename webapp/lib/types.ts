/* =============================================================================
 *  lib/types.ts - shapes matching firebase/database.rules.json exactly.
 *  Realtime Database has no separate row "id" column - the path segment
 *  itself is the key (a device's MAC, a pair's push id, a role letter), so
 *  these interfaces carry that key as an explicit field only where it's
 *  convenient to have alongside the rest of the object after a fetch.
 * ========================================================================== */

export type Role = "A" | "B";
export type Mood = "normal" | "miss" | "sleep" | "work" | "hug";

/** /pairs/{pairId} */
export interface Pair {
  pairId: string;
  pairCode: string;
  ownerId: string;
  partnerId: string | null;
  deviceAuthA: string | null;
  deviceAuthB: string | null;
  deviceNameA: string | null;
  deviceNameB: string | null;
  createdAt: number;
  updatedAt: number;
}

/** /devices/{mac} */
export interface Device {
  mac: string;
  authUid: string;
  userId: string | null;
  pairId: string | null;
  role: Role | null;
  name: string;
  createdAt: number;
  lastSeenAt: number | null;
}

/** /lamp_sync/{pairId}/{role} */
export interface LampSync {
  deviceId: string | null;
  power: boolean;
  color: string; // "#rrggbb"
  bright: number; // 5-255
  mood: Mood;
  note: string;
  vib: boolean;
  track: number;
  alert: string;
  updatedAt: number;
}

export const DEFAULT_LAMP_SYNC: LampSync = {
  deviceId: null,
  power: true,
  color: "#ff4d8d",
  bright: 160,
  mood: "normal",
  note: "",
  vib: false,
  track: 0,
  alert: "",
  updatedAt: 0,
};

/** Mirrors Config.h PARTNER_STALE_MS on the firmware - how long a lamp still
 *  counts as online on the strength of its last update alone. */
export const PARTNER_STALE_MS = 180_000;

export function isRecentlyUpdated(updatedAtMs: number | null, withinMs = PARTNER_STALE_MS): boolean {
  if (!updatedAtMs) return false;
  return Date.now() - updatedAtMs < withinMs;
}

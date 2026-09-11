"use client";

import { useEffect, useRef, useState } from "react";
import { ref, onValue } from "firebase/database";
import { getFirebaseDb } from "@/lib/firebase/client";
import { updateLampState, sendBuzz, leavePair } from "@/lib/actions";
import type { Device, Pair, LampSync, Mood, Role } from "@/lib/types";
import { DEFAULT_LAMP_SYNC, isRecentlyUpdated } from "@/lib/types";

const MOODS: { key: Mood; label: string; hint: string; dot: string }[] = [
  { key: "normal", label: "Together", hint: "back to your own colour", dot: "#ff5c8a" },
  { key: "miss", label: "Miss You", hint: "slow pink breathing", dot: "#ff3c8c" },
  { key: "sleep", label: "Sleeping", hint: "dim amber, plays the lullaby", dot: "#ff781e" },
  { key: "work", label: "Working", hint: "steady blue, no interruptions", dot: "#1e78ff" },
  { key: "hug", label: "Need a Hug", hint: "red heartbeat pulse", dot: "#ff283c" },
];

const SWATCHES = ["#ff5c8a", "#ff8a5c", "#ffc46b", "#7ee0c0", "#5cc8ff", "#9d7bff", "#ff4040", "#ffeede"];

function otherRole(role: Role): Role {
  return role === "A" ? "B" : "A";
}

interface Props {
  device: Device;
  pair: Pair;
  onChanged?: () => void;
}

export default function LampDashboard({ device, pair, onChanged }: Props) {
  const myRole = device.role as Role;
  const partnerRole = otherRole(myRole);
  const partnerName = myRole === "A" ? pair.deviceNameB : pair.deviceNameA;
  const pairHasPartner = !!pair.partnerId;

  const [myLamp, setMyLamp] = useState<LampSync>(DEFAULT_LAMP_SYNC);
  const [partnerLamp, setPartnerLamp] = useState<LampSync | null>(null);
  const [note, setNote] = useState("");
  const [toast, setToast] = useState<string | null>(null);
  const [, setTick] = useState(0); // forces a re-render so "online" staleness re-evaluates over time
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const showToast = (msg: string) => {
    setToast(msg);
    setTimeout(() => setToast((t) => (t === msg ? null : t)), 2200);
  };

  useEffect(() => {
    const id = setInterval(() => setTick((t) => t + 1), 5000);
    return () => clearInterval(id);
  }, []);

  // Realtime Database listeners - both this lamp's own row (so a change
  // made by its physical touch sensor or local Web UI shows up here too)
  // and the partner's, arriving the instant either changes. No polling.
  useEffect(() => {
    const db = getFirebaseDb();
    const myRef = ref(db, `lamp_sync/${pair.pairId}/${myRole}`);
    const partnerRef = ref(db, `lamp_sync/${pair.pairId}/${partnerRole}`);

    const unsubMine = onValue(myRef, (snap) => {
      if (snap.exists()) setMyLamp(snap.val() as LampSync);
    });
    const unsubPartner = onValue(partnerRef, (snap) => {
      setPartnerLamp(snap.exists() ? (snap.val() as LampSync) : null);
    });

    return () => {
      unsubMine();
      unsubPartner();
    };
  }, [pair.pairId, myRole, partnerRole]);

  function patchOptimistic(patch: Partial<LampSync>) {
    setMyLamp((prev) => ({ ...prev, ...patch }));
  }

  function debouncedSend(patch: Parameters<typeof updateLampState>[2]) {
    if (debounceRef.current) clearTimeout(debounceRef.current);
    debounceRef.current = setTimeout(() => {
      updateLampState(pair.pairId, myRole, patch);
    }, 250);
  }

  const togglePower = () => {
    const power = !myLamp.power;
    patchOptimistic({ power });
    updateLampState(pair.pairId, myRole, { power });
  };

  const setColor = (color: string) => {
    patchOptimistic({ color });
    debouncedSend({ color });
  };

  const setBright = (bright: number) => {
    patchOptimistic({ bright });
    debouncedSend({ bright });
  };

  const setMood = (mood: Mood) => {
    patchOptimistic({ mood });
    updateLampState(pair.pairId, myRole, { mood });
    showToast("They can see how you feel");
  };

  const send = async () => {
    const v = note.trim();
    if (!v) return showToast("Write something first");
    await updateLampState(pair.pairId, myRole, { note: v });
    setNote("");
    showToast("Note sent");
  };

  const buzz = async () => {
    await sendBuzz(pair.pairId, myRole);
    showToast("They just felt that");
  };

  const copyCode = async () => {
    try {
      await navigator.clipboard.writeText(pair.pairCode);
      showToast("Pair code copied");
    } catch {
      showToast("Copy failed - select and copy manually");
    }
  };

  const leave = async () => {
    if (!confirm("Unpair this lamp? Your partner will stop hearing from it.")) return;
    const res = await leavePair(device.mac);
    if (res.error) showToast(res.error);
    else onChanged?.();
  };

  const partnerOnline = isRecentlyUpdated(partnerLamp?.updatedAt ?? null);

  return (
    <div className="space-y-6">
      {/* Partner status */}
      <div className="flex items-center justify-between text-sm">
        <div className="flex items-center gap-2">
          <span
            className={`inline-block w-2 h-2 rounded-full ${partnerOnline ? "bg-rose shadow-[0_0_8px] shadow-rose" : "bg-[#5b4350]"}`}
          />
          <span className="text-mut">
            {pairHasPartner
              ? partnerOnline
                ? `${partnerName ?? "Your partner"}'s lamp is online`
                : `${partnerName ?? "Your partner"}'s lamp hasn't checked in recently`
              : "Waiting for your partner to join"}
          </span>
        </div>
        <span className="text-xs text-mut">role {myRole}</span>
      </div>

      {!pairHasPartner && (
        <div className="rounded-xl border border-line bg-ink px-4 py-3">
          <p className="text-xs text-mut mb-2">Share this code with your partner so they can join:</p>
          <div className="flex items-center gap-2">
            <code className="text-lg tracking-[0.3em] text-rose">{pair.pairCode}</code>
            <button className="btn-ghost w-auto px-3 py-1.5 text-xs" onClick={copyCode}>
              Copy
            </button>
          </div>
        </div>
      )}

      {/* Light */}
      <div>
        <h3 className="label mt-0">Colour</h3>
        <div className="grid grid-cols-8 gap-2">
          {SWATCHES.map((c) => (
            <button
              key={c}
              aria-label={c}
              onClick={() => setColor(c)}
              style={{ background: c }}
              className={`aspect-square rounded-full shadow-inner ${myLamp.color === c ? "ring-2 ring-txt ring-offset-2 ring-offset-ink2" : ""}`}
            />
          ))}
        </div>
        <div className="flex items-center gap-3 mt-3">
          <input
            type="color"
            value={myLamp.color}
            onChange={(e) => setColor(e.target.value)}
            className="w-12 h-10 p-1 rounded-lg flex-shrink-0"
            aria-label="Custom colour"
          />
          <span className="text-xs text-mut">Pick any other colour</span>
        </div>

        <h3 className="label">Brightness</h3>
        <input
          type="range"
          min={5}
          max={255}
          value={myLamp.bright}
          onChange={(e) => setBright(Number(e.target.value))}
          className="w-full"
        />

        <div className="flex gap-2 mt-4">
          <button className="btn-ghost" onClick={togglePower}>
            {myLamp.power ? "Turn off" : "Turn on"}
          </button>
          <button className="btn-ghost" onClick={buzz}>
            Send a buzz
          </button>
        </div>
      </div>

      {/* Mood */}
      <div>
        <h3 className="label">How are you feeling</h3>
        <div className="space-y-1">
          {MOODS.map((m) => (
            <button
              key={m.key}
              onClick={() => setMood(m.key)}
              className={`flex items-center gap-3 w-full text-left border-b border-line last:border-0 py-3 ${myLamp.mood === m.key ? "text-rose" : "text-txt"}`}
            >
              <span className="w-3.5 h-3.5 rounded-full flex-shrink-0" style={{ background: m.dot }} />
              <span className="flex-1">
                <strong className="block text-sm font-semibold">{m.label}</strong>
                <span className="block text-xs text-mut mt-0.5">{m.hint}</span>
              </span>
              {myLamp.mood === m.key && <span className="text-rose">✓</span>}
            </button>
          ))}
        </div>
      </div>

      {/* Notes */}
      <div>
        <h3 className="label">Secret love note</h3>
        <textarea
          rows={3}
          maxLength={96}
          value={note}
          onChange={(e) => setNote(e.target.value)}
          placeholder="Type something sweet…"
          className="field"
        />
        <button className="btn mt-3" onClick={send}>
          Send it over
        </button>
        <h3 className="label">Last note you received</h3>
        <p className="font-serif italic text-lg border-l-2 border-rose pl-4 py-0.5">
          {partnerLamp?.note || "—"}
        </p>
      </div>

      <button
        className="text-xs text-mut border border-line rounded-full px-3 py-1.5 hover:text-rose hover:border-rose transition"
        onClick={leave}
      >
        {pairHasPartner ? "Unpair this lamp" : "Cancel pairing"}
      </button>

      {toast && (
        <div className="fixed left-1/2 bottom-6 -translate-x-1/2 bg-[#3b1424] border border-rose text-txt px-5 py-3 rounded-full text-sm z-50">
          {toast}
        </div>
      )}
    </div>
  );
}

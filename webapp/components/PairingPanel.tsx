"use client";

import { useState, useTransition } from "react";
import { createPair, joinPair } from "@/lib/actions";

export default function PairingPanel({ mac, onChanged }: { mac: string; onChanged?: () => void }) {
  const [mode, setMode] = useState<"choose" | "join">("choose");
  const [code, setCode] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [pending, startTransition] = useTransition();

  function create() {
    setError(null);
    startTransition(async () => {
      const res = await createPair(mac);
      if (res.error) setError(res.error);
      else onChanged?.();
    });
  }

  function join() {
    if (!code.trim()) {
      setError("Enter a pair code.");
      return;
    }
    setError(null);
    startTransition(async () => {
      const res = await joinPair(mac, code);
      if (res.error) setError(res.error);
      else onChanged?.();
    });
  }

  return (
    <div>
      <h3 className="label mt-0">Pairing</h3>

      {error && (
        <div className="mb-3 rounded-xl border border-rose bg-[#3b1220] text-[#ffdbe4] text-sm px-4 py-3">
          {error}
        </div>
      )}

      {mode === "choose" ? (
        <div className="grid gap-2">
          <button className="btn" onClick={create} disabled={pending}>
            {pending ? "Starting…" : "Start a new pair"}
          </button>
          <button className="btn-ghost" onClick={() => setMode("join")} disabled={pending}>
            I have a pair code
          </button>
          <p className="hint">
            Starting a pair gives you a code to share with your partner. If they already started one,
            enter their code instead.
          </p>
        </div>
      ) : (
        <div>
          <label className="label mt-0" htmlFor={`code-${mac}`}>
            Pair code from your partner
          </label>
          <input
            id={`code-${mac}`}
            className="field font-mono tracking-[0.3em] uppercase text-center text-lg"
            maxLength={8}
            value={code}
            onChange={(e) => setCode(e.target.value.toUpperCase())}
            placeholder="XXXXXXXX"
            autoCapitalize="characters"
            autoComplete="off"
          />
          <div className="grid gap-2 mt-3">
            <button className="btn" onClick={join} disabled={pending}>
              {pending ? "Joining…" : "Join pair"}
            </button>
            <button
              className="btn-ghost"
              onClick={() => {
                setMode("choose");
                setError(null);
              }}
              disabled={pending}
            >
              Back
            </button>
          </div>
        </div>
      )}
    </div>
  );
}

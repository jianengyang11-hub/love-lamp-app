"use client";

import { useState, type FormEvent } from "react";
import { registerDevice } from "@/lib/actions";

export default function DeviceRegisterForm({ onChanged }: { onChanged?: () => void }) {
  const [mac, setMac] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [pending, setPending] = useState(false);

  async function onSubmit(e: FormEvent) {
    e.preventDefault();
    setError(null);
    setPending(true);

    const res = await registerDevice(mac);

    setPending(false);
    if (res.error) {
      setError(res.error);
      return;
    }
    setMac("");
    onChanged?.();
  }

  return (
    <form onSubmit={onSubmit}>
      {error && (
        <div className="mb-3 rounded-xl border border-rose bg-[#3b1220] text-[#ffdbe4] text-sm px-4 py-3">
          {error}
        </div>
      )}
      <label className="label" htmlFor="mac">
        MAC address
      </label>
      <input
        className="field font-mono"
        id="mac"
        name="mac"
        placeholder="AA:BB:CC:DD:EE:FF"
        autoComplete="off"
        autoCapitalize="characters"
        required
        value={mac}
        onChange={(e) => setMac(e.target.value)}
      />
      <div className="mt-4">
        <button className="btn" type="submit" disabled={pending}>
          {pending ? "Registering…" : "Register lamp"}
        </button>
      </div>
    </form>
  );
}

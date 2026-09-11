"use client";

import { useState, useTransition } from "react";
import { renameDevice, unregisterDevice } from "@/lib/actions";
import type { Device } from "@/lib/types";
import { isRecentlyUpdated } from "@/lib/types";

export default function DeviceSettings({
  device,
  onChanged,
}: {
  device: Device;
  onChanged?: () => void;
}) {
  const [name, setName] = useState(device.name);
  const [editing, setEditing] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [pending, startTransition] = useTransition();

  const online = isRecentlyUpdated(device.lastSeenAt);

  function save() {
    startTransition(async () => {
      const res = await renameDevice(device.mac, name);
      if (res.error) {
        setError(res.error);
      } else {
        setError(null);
        setEditing(false);
        onChanged?.();
      }
    });
  }

  function remove() {
    if (!confirm(`Remove "${device.name}"? This unpairs it and cannot be undone.`)) return;
    startTransition(async () => {
      const res = await unregisterDevice(device.mac);
      if (res.error) setError(res.error);
      else onChanged?.();
    });
  }

  return (
    <div>
      <div className="flex items-start justify-between gap-3">
        <div className="min-w-0 flex-1">
          {editing ? (
            <div className="flex gap-2">
              <input
                className="field"
                value={name}
                onChange={(e) => setName(e.target.value)}
                maxLength={40}
                autoFocus
                onKeyDown={(e) => e.key === "Enter" && save()}
              />
              <button className="btn-ghost w-auto px-4 flex-shrink-0" onClick={save} disabled={pending}>
                Save
              </button>
            </div>
          ) : (
            <h2
              className="font-serif text-2xl truncate cursor-pointer"
              onClick={() => setEditing(true)}
              title="Click to rename"
            >
              {device.name}
            </h2>
          )}
          <div className="flex items-center gap-2 mt-1.5">
            <span
              className={`inline-block w-1.5 h-1.5 rounded-full ${online ? "bg-rose shadow-[0_0_6px] shadow-rose" : "bg-[#5b4350]"}`}
            />
            <p className="text-xs text-mut font-mono">{device.mac}</p>
          </div>
          <p className="text-xs text-mut mt-0.5">
            {device.lastSeenAt
              ? `Last heard from ${new Date(device.lastSeenAt).toLocaleString()}`
              : "Never heard from yet - power it on and connect it to Wi-Fi"}
          </p>
        </div>
        <button
          className="text-xs text-mut border border-line rounded-full px-3 py-1.5 hover:text-rose hover:border-rose transition flex-shrink-0"
          onClick={remove}
          disabled={pending}
        >
          Remove
        </button>
      </div>
      {error && <p className="text-xs text-rose mt-2">{error}</p>}
    </div>
  );
}

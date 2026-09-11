"use client";

import { useCallback, useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import { onAuthStateChanged, type User } from "firebase/auth";
import { ref, get } from "firebase/database";
import { getFirebaseAuth, getFirebaseDb } from "@/lib/firebase/client";
import type { Device, Pair } from "@/lib/types";
import DeviceRegisterForm from "@/components/DeviceRegisterForm";
import PairingPanel from "@/components/PairingPanel";
import LampDashboard from "@/components/LampDashboard";
import DeviceSettings from "@/components/DeviceSettings";
import SignOutButton from "@/components/SignOutButton";

export default function DashboardPage() {
  const router = useRouter();

  // undefined = auth check still in flight, null = signed out (redirecting)
  const [user, setUser] = useState<User | null | undefined>(undefined);
  const [devices, setDevices] = useState<Device[]>([]);
  const [pairs, setPairs] = useState<Record<string, Pair>>({});
  const [loadError, setLoadError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);

  // Realtime Database rules can't filter a list query per-row the way
  // Postgres RLS could - "which devices are mine" has to come from the
  // denormalized /userDevices/{uid} index rather than a query over
  // /devices (see firebase/database.rules.json). Each device is then a
  // direct, individually-authorized read.
  const loadData = useCallback(async (uid: string) => {
    const db = getFirebaseDb();
    try {
      const indexSnap = await get(ref(db, `userDevices/${uid}`));
      const macs = indexSnap.exists() ? Object.keys(indexSnap.val()) : [];

      const deviceList = (
        await Promise.all(
          macs.map(async (mac) => {
            const snap = await get(ref(db, `devices/${mac}`));
            return snap.exists() ? ({ ...snap.val(), mac } as Device) : null;
          })
        )
      ).filter((d): d is Device => d !== null);

      const pairIds = Array.from(new Set(deviceList.map((d) => d.pairId).filter((p): p is string => !!p)));
      const pairEntries = await Promise.all(
        pairIds.map(async (pairId) => {
          const snap = await get(ref(db, `pairs/${pairId}`));
          return [pairId, snap.exists() ? ({ ...snap.val(), pairId } as Pair) : null] as const;
        })
      );

      setDevices(deviceList);
      setPairs(Object.fromEntries(pairEntries.filter(([, p]) => p !== null)) as Record<string, Pair>);
      setLoadError(null);
    } catch (err) {
      setLoadError(err instanceof Error ? err.message : "Couldn't load your lamps.");
    }
  }, []);

  // Auth guard - there is no server here to check this for us (static
  // export), so every visit to /dashboard checks the current Firebase Auth
  // state on mount and keeps listening for it to change (sign-out in
  // another tab, a revoked session, etc).
  useEffect(() => {
    const unsubscribe = onAuthStateChanged(getFirebaseAuth(), (u) => {
      if (!u) {
        setUser(null);
        router.replace("/login");
      } else {
        setUser(u);
      }
    });
    return unsubscribe;
  }, [router]);

  useEffect(() => {
    if (!user) return;
    let active = true;
    setLoading(true);
    loadData(user.uid).finally(() => {
      if (active) setLoading(false);
    });
    return () => {
      active = false;
    };
  }, [user, loadData]);

  const refresh = useCallback(() => {
    if (user) loadData(user.uid);
  }, [user, loadData]);

  if (user === undefined || (user && loading)) {
    return (
      <main className="text-center py-20">
        <p className="text-mut">Loading…</p>
      </main>
    );
  }
  if (!user) return null; // redirect to /login already in flight

  return (
    <>
      <header className="flex items-center justify-between mb-8">
        <h1 className="font-serif text-3xl">
          <span className="italic text-rose">Love</span> Lamp
        </h1>
        <div className="flex items-center gap-3">
          <span className="text-xs text-mut hidden sm:inline">{user.phoneNumber}</span>
          <SignOutButton />
        </div>
      </header>

      <main className="space-y-6">
        {loadError && (
          <div className="rounded-xl border border-rose bg-[#3b1220] text-[#ffdbe4] text-sm px-4 py-3">
            Couldn&apos;t load your lamps: {loadError}
          </div>
        )}

        {devices.length === 0 && (
          <div className="card">
            <h2 className="font-serif text-2xl mb-2">Welcome</h2>
            <p className="text-sm text-mut">
              Power on your lamp and connect it to Wi-Fi through its setup portal. Once it&apos;s online, its
              OLED screen shows its MAC address and a message asking to be paired - enter that address below
              to claim it.
            </p>
          </div>
        )}

        {devices.map((device) => {
          const pair = device.pairId ? pairs[device.pairId] ?? null : null;

          return (
            <section key={device.mac} className="card space-y-5">
              <DeviceSettings device={device} onChanged={refresh} />

              {device.pairId && device.role && pair ? (
                <LampDashboard device={device} pair={pair} onChanged={refresh} />
              ) : (
                <PairingPanel mac={device.mac} onChanged={refresh} />
              )}
            </section>
          );
        })}

        <div className="card">
          <h2 className="font-serif text-xl mb-1">{devices.length === 0 ? "Register your lamp" : "Register another lamp"}</h2>
          <p className="text-xs text-mut mb-4">
            Bind an ESP32 to this account by its MAC address, shown on its OLED once it&apos;s connected to
            Wi-Fi.
          </p>
          <DeviceRegisterForm onChanged={refresh} />
        </div>
      </main>
    </>
  );
}

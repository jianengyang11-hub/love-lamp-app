"use client";

import { useEffect } from "react";
import { useRouter } from "next/navigation";
import { onAuthStateChanged } from "firebase/auth";
import { getFirebaseAuth } from "@/lib/firebase/client";

export default function HomePage() {
  const router = useRouter();

  useEffect(() => {
    const unsubscribe = onAuthStateChanged(getFirebaseAuth(), (user) => {
      router.replace(user ? "/dashboard" : "/login");
    });
    return unsubscribe;
  }, [router]);

  return (
    <main className="text-center py-20">
      <p className="text-mut">Loading…</p>
    </main>
  );
}

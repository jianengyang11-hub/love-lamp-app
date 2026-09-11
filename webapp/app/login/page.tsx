"use client";

import { useEffect, useRef, useState, type FormEvent } from "react";
import { useRouter } from "next/navigation";
import { RecaptchaVerifier, signInWithPhoneNumber, type ConfirmationResult } from "firebase/auth";
import { getFirebaseAuth } from "@/lib/firebase/client";

/**
 * Phone number sign-in (OTP), the only auth method this app has - there is
 * no separate sign-up: `signInWithPhoneNumber` + `confirm()` creates the
 * Firebase Auth account automatically the first time a number verifies, and
 * signs an existing one back in on every later visit. See
 * firebase/README.md's "Why two different Auth providers" for why the
 * ESP32 firmware, which obviously can't receive an SMS, uses a completely
 * different provider (email/password) for its own separate identity.
 */
export default function LoginPage() {
  const router = useRouter();
  const recaptchaContainerRef = useRef<HTMLDivElement>(null);
  const verifierRef = useRef<RecaptchaVerifier | null>(null);
  const confirmationRef = useRef<ConfirmationResult | null>(null);

  const [step, setStep] = useState<"phone" | "code">("phone");
  const [phone, setPhone] = useState("");
  const [code, setCode] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [pending, setPending] = useState(false);
  const [recaptchaReady, setRecaptchaReady] = useState(false);

  // The verifier is created once and reused for the lifetime of this page -
  // Firebase renders an invisible widget into the container the moment
  // it's constructed, ready for signInWithPhoneNumber() to trigger it later.
  useEffect(() => {
    if (!recaptchaContainerRef.current || verifierRef.current) return;

    const verifier = new RecaptchaVerifier(getFirebaseAuth(), recaptchaContainerRef.current, {
      size: "invisible",
    });
    verifierRef.current = verifier;
    verifier.render().then(() => setRecaptchaReady(true));

    return () => {
      verifier.clear();
      verifierRef.current = null;
    };
  }, []);

  function resetRecaptcha() {
    // Per Firebase's own guidance: a failed signInWithPhoneNumber call
    // leaves the invisible widget in a used state - reset it so the next
    // attempt can trigger a fresh challenge instead of silently failing.
    const verifier = verifierRef.current;
    if (!verifier) return;
    verifier.render().then((widgetId) => {
      const grecaptcha = (window as unknown as { grecaptcha?: { reset: (id: number) => void } }).grecaptcha;
      grecaptcha?.reset(widgetId);
    });
  }

  async function sendCode(e: FormEvent) {
    e.preventDefault();
    setError(null);

    const trimmed = phone.trim();
    if (!/^\+[1-9]\d{7,14}$/.test(trimmed)) {
      setError("Enter your phone number in international format, e.g. +14155552671.");
      return;
    }
    if (!verifierRef.current) {
      setError("Still loading - try again in a moment.");
      return;
    }

    setPending(true);
    try {
      confirmationRef.current = await signInWithPhoneNumber(getFirebaseAuth(), trimmed, verifierRef.current);
      setStep("code");
    } catch (err) {
      setError(err instanceof Error ? err.message : "Couldn't send a code to that number.");
      resetRecaptcha();
    } finally {
      setPending(false);
    }
  }

  async function verifyCode(e: FormEvent) {
    e.preventDefault();
    setError(null);

    if (!confirmationRef.current) {
      setError("Request a code first.");
      setStep("phone");
      return;
    }
    const trimmed = code.trim();
    if (!/^\d{6}$/.test(trimmed)) {
      setError("Enter the 6-digit code from the text message.");
      return;
    }

    setPending(true);
    try {
      await confirmationRef.current.confirm(trimmed);
      router.push("/dashboard");
    } catch (err) {
      setError(err instanceof Error ? err.message : "That code didn't work - check it and try again.");
    } finally {
      setPending(false);
    }
  }

  return (
    <main>
      <header className="text-center mb-8">
        <h1 className="font-serif text-4xl">
          <span className="italic text-rose">Love</span> Lamp
        </h1>
        <p className="text-mut text-sm mt-2">Sign in with your phone to pair and control your lamp.</p>
      </header>

      <div className="card">
        {error && (
          <div className="mb-4 rounded-xl border border-rose bg-[#3b1220] text-[#ffdbe4] text-sm px-4 py-3">
            {error}
          </div>
        )}

        {step === "phone" ? (
          <form onSubmit={sendCode}>
            <label className="label mt-0" htmlFor="phone">
              Phone number
            </label>
            <input
              className="field"
              id="phone"
              name="phone"
              type="tel"
              inputMode="tel"
              autoComplete="tel"
              placeholder="+14155552671"
              required
              value={phone}
              onChange={(e) => setPhone(e.target.value)}
            />
            <p className="hint">Include the country code, e.g. +1 for the US, +66 for Thailand.</p>
            <div className="mt-6">
              <button className="btn" type="submit" disabled={pending || !recaptchaReady}>
                {pending ? "Sending…" : recaptchaReady ? "Send code" : "Loading…"}
              </button>
            </div>
          </form>
        ) : (
          <form onSubmit={verifyCode}>
            <label className="label mt-0" htmlFor="code">
              6-digit code
            </label>
            <input
              className="field text-center text-2xl tracking-[0.4em] font-mono"
              id="code"
              name="code"
              type="text"
              inputMode="numeric"
              autoComplete="one-time-code"
              maxLength={6}
              placeholder="000000"
              required
              autoFocus
              value={code}
              onChange={(e) => setCode(e.target.value.replace(/\D/g, "").slice(0, 6))}
            />
            <p className="hint">Sent to {phone}.</p>
            <div className="mt-6 grid gap-2">
              <button className="btn" type="submit" disabled={pending}>
                {pending ? "Verifying…" : "Verify & sign in"}
              </button>
              <button
                className="btn-ghost"
                type="button"
                disabled={pending}
                onClick={() => {
                  setStep("phone");
                  setCode("");
                  setError(null);
                }}
              >
                Use a different number
              </button>
            </div>
          </form>
        )}

        {/* Required by RecaptchaVerifier - stays empty/invisible in normal use. */}
        <div ref={recaptchaContainerRef} id="recaptcha-container" />
      </div>
    </main>
  );
}

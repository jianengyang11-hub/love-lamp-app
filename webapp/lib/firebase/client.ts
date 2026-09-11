import { initializeApp, getApps, type FirebaseApp } from "firebase/app";
import { getAuth, type Auth } from "firebase/auth";
import { getDatabase, type Database } from "firebase/database";

/**
 * The one Firebase app instance in this project - there is no server
 * runtime on a static export (Cloudflare Pages serves plain files), so
 * every page is a Client Component and every query, mutation, auth call
 * and Realtime Database listener goes through the `auth`/`db` singletons
 * exported here.
 *
 * NEXT_PUBLIC_FIREBASE_* are inlined into the built JS at `next build` time
 * (see next.config.js's `output: "export"` and the README) - set them as
 * build-time environment variables in the Cloudflare Pages project, not as
 * "Functions" runtime variables, since there is no runtime to read them
 * from.
 */
function requireEnv(name: string): string {
  const value = process.env[name];
  if (!value) {
    throw new Error(
      `Missing ${name} - copy .env.local.example to .env.local for local dev, and set the same ` +
        "names as build-time variables in Cloudflare Pages."
    );
  }
  return value;
}

let app: FirebaseApp;
let authInstance: Auth;
let dbInstance: Database;

function ensureApp(): FirebaseApp {
  if (getApps().length > 0) {
    app = getApps()[0]!;
    return app;
  }

  app = initializeApp({
    apiKey: requireEnv("NEXT_PUBLIC_FIREBASE_API_KEY"),
    authDomain: requireEnv("NEXT_PUBLIC_FIREBASE_AUTH_DOMAIN"),
    databaseURL: requireEnv("NEXT_PUBLIC_FIREBASE_DATABASE_URL"),
    projectId: requireEnv("NEXT_PUBLIC_FIREBASE_PROJECT_ID"),
    appId: requireEnv("NEXT_PUBLIC_FIREBASE_APP_ID"),
  });
  return app;
}

export function getFirebaseAuth(): Auth {
  if (!authInstance) authInstance = getAuth(ensureApp());
  return authInstance;
}

export function getFirebaseDb(): Database {
  if (!dbInstance) dbInstance = getDatabase(ensureApp());
  return dbInstance;
}

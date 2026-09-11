import type { Metadata, Viewport } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Love Lamp",
  description: "Account, pairing and live dashboard for your Love Lamp.",
};

export const viewport: Viewport = {
  themeColor: "#14090f",
  width: "device-width",
  initialScale: 1,
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <head>
        <link
          rel="stylesheet"
          href="https://fonts.googleapis.com/css2?family=Instrument+Serif:ital@0;1&display=swap"
        />
      </head>
      <body className="min-h-screen">
        <div className="mx-auto max-w-xl px-4 pb-16 pt-8">{children}</div>
      </body>
    </html>
  );
}

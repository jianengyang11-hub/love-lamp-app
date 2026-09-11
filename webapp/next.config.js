/** @type {import('next').NextConfig} */
const nextConfig = {
  // Static export: Cloudflare Pages serves this app as plain HTML/JS/CSS,
  // with no Node.js server behind it. Every page below is a Client
  // Component that talks to Supabase directly from the browser - there is
  // no Server Component data-fetching, no Server Actions, no Route
  // Handlers and no middleware in this app, because none of those run on a
  // static host.
  output: "export",

  // Next's Image Optimization API needs a server to resize images on
  // request, which a static export doesn't have. This app doesn't use
  // next/image, but the flag is set for correctness/completeness.
  images: { unoptimized: true },

  reactStrictMode: true,
};

module.exports = nextConfig;

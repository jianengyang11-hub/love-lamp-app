import type { Config } from "tailwindcss";

const config: Config = {
  content: [
    "./app/**/*.{ts,tsx}",
    "./components/**/*.{ts,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        ink: "#14090f",
        ink2: "#1e1119",
        line: "#33202b",
        rose: "#ff5c8a",
        ember: "#f0954e",
        txt: "#f7eaef",
        mut: "#a98a98",
      },
      fontFamily: {
        serif: ['"Instrument Serif"', "Georgia", "serif"],
      },
    },
  },
  plugins: [],
};

export default config;

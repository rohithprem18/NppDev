import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { fileURLToPath } from "node:url";

const monaco = fileURLToPath(new URL("./node_modules/monaco-editor/esm/vs/", import.meta.url));

// https://v2.tauri.app/start/frontend/vite/
export default defineConfig({
  plugins: [react()],
  resolve: { alias: [{ find: /^@monaco\//, replacement: monaco }] },
  clearScreen: false,
  server: { port: 1420, strictPort: true, watch: { ignored: ["**/src-tauri/**"] } },
  envPrefix: ["VITE_", "TAURI_ENV_"],
  build: {
    target: process.env.TAURI_ENV_PLATFORM === "windows" ? "chrome105" : "safari13",
    sourcemap: false,
    chunkSizeWarningLimit: 4000,
  },
});

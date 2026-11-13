import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "./tests/browser",
  use: {
    baseURL: "http://127.0.0.1:4173/orbit-synth/",
    channel: process.env.CI ? undefined : "msedge",
  },
  webServer: {
    command: "node tests/server.mjs",
    url: "http://127.0.0.1:4173/orbit-synth/",
    reuseExistingServer: false,
  },
});

import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "./tests",
  timeout: 30_000,
  use: {
    browserName: "chromium",
    channel: process.env.POC05_BROWSER_CHANNEL || undefined,
    headless: process.env.POC05_HEADED !== "1",
    viewport: { width: 1024, height: 768 },
    deviceScaleFactor: 1,
    trace: process.env.POC05_TRACE === "1" ? "on" : "retain-on-failure",
  },
  reporter: [["line"], ["json", { outputFile: "test-results/web-result.json" }]],
});

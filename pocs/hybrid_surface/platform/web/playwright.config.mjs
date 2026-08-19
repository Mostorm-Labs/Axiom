import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "./tests",
  timeout: 30_000,
  use: {
    browserName: "chromium",
    viewport: { width: 1024, height: 768 },
    deviceScaleFactor: 1,
  },
  reporter: [["line"], ["json", { outputFile: "test-results/web-result.json" }]],
});

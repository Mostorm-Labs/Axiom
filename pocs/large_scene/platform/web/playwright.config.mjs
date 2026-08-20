import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: ".",
  testMatch: "poc03.spec.mjs",
  use: {
    headless: true,
    launchOptions: process.env.POC03_CHROME_EXECUTABLE ? {
      executablePath: process.env.POC03_CHROME_EXECUTABLE,
    } : {},
  },
  reporter: "line"
});

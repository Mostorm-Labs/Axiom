import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "tests",
  use: {
    headless: true,
    channel: process.env.CI ? "chrome" : undefined,
  },
});

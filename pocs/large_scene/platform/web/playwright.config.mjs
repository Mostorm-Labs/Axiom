import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: ".",
  testMatch: "poc03.spec.mjs",
  use: { headless: true },
  reporter: "line"
});

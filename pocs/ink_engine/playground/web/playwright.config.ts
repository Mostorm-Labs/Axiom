import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "./tests",
  timeout: 180_000,
  use: { baseURL: "http://127.0.0.1:4273", trace: "retain-on-failure" },
  webServer: {
    command: "npm run dev",
    url: "http://127.0.0.1:4273",
    reuseExistingServer: false,
  },
  projects: [{
    name: "chromium-swiftshader",
    use: {
      browserName: "chromium",
      launchOptions: {
        args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"],
      },
    },
  }],
});

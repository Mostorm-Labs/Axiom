import { chromium } from "@playwright/test";
import fs from "node:fs/promises";
import path from "node:path";

const output = process.argv[2];
if (!output) throw new Error("usage: node tools/write_web_result.mjs OUTPUT.json");
const browser = await chromium.launch({
  args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"],
});
const page = await browser.newPage();
await page.goto("http://127.0.0.1:4273");
await page.getByTestId("status").waitFor({ state: "visible" });
await page.getByRole("button", { name: "Replay Vector" }).click();
await page.waitForFunction(() => /^[0-9a-f]{32}$/.test(
  document.querySelector('[data-testid="document-digest"]')?.textContent ?? ""));
await page.getByTestId("golden").waitFor();
await page.waitForFunction(() =>
  document.querySelector('[data-testid="golden"]')?.textContent?.includes("within ±2"));
const capture = () => page.evaluate(() => {
  const text = (name) => document.querySelector(`[data-testid="${name}"]`)?.textContent ?? "";
  const metrics = text("golden");
  const ratio = Number(metrics.match(/^([0-9.]+)%/)?.[1] ?? 0) / 100;
  const delta = Number(metrics.match(/max Δ ([0-9]+)/)?.[1] ?? 255);
  return {
    platform: "web",
    backend: "ganesh-webgl2-swiftshader",
    document_digest: text("document-digest"),
    stroke_digest: text("stroke-digest"),
    preview_digest: text("preview-digest"),
    numeric_digest: text("numeric-digest"),
    matching_ratio: ratio,
    maximum_channel_delta: delta,
  };
});
const vectorResult = await capture();
await page.getByRole("button", { name: "Replay Dab" }).click();
await page.waitForFunction((vectorDigest) => {
  const value = document.querySelector('[data-testid="stroke-digest"]')?.textContent ?? "";
  return /^[0-9a-f]{32}$/.test(value) && value !== vectorDigest;
}, vectorResult.stroke_digest);
await page.waitForFunction(() =>
  document.querySelector('[data-testid="status"]')?.textContent?.includes(
    "dab-turn.ndjson replayed"));
const dabResult = await capture();
const result = {
  ...vectorResult,
  dab_document_digest: dabResult.document_digest,
  dab_stroke_digest: dabResult.stroke_digest,
  dab_preview_digest: dabResult.preview_digest,
  dab_matching_ratio: dabResult.matching_ratio,
  dab_maximum_channel_delta: dabResult.maximum_channel_delta,
};
await fs.mkdir(path.dirname(output), { recursive: true });
await fs.writeFile(output, JSON.stringify(result, null, 2) + "\n");
console.log(JSON.stringify(result));
await browser.close();

import { expect, test } from "@playwright/test";

test("replays deterministic Vector and Dab fixtures", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByTestId("status")).toContainText("coalesced input stays in C++");
  await page.getByRole("button", { name: "Replay Vector" }).click();
  await expect(page.getByTestId("samples")).toHaveText("4");
  await expect(page.getByTestId("stroke-digest")).toHaveText(/^[0-9a-f]{32}$/);
  await expect(page.getByTestId("document-digest")).toHaveText(/^[0-9a-f]{32}$/);
  const vectorDigest = await page.getByTestId("stroke-digest").textContent();
  const vectorDocument = await page.getByTestId("document-digest").textContent();
  expect(vectorDigest).toMatch(/^[0-9a-f]{32}$/);
  expect(vectorDocument).toMatch(/^[0-9a-f]{32}$/);
  await expect(page.getByTestId("golden")).toContainText("within ±2");
  await page.getByRole("button", { name: "Replay Dab" }).click();
  await expect(page.getByTestId("samples")).toHaveText("4");
  await expect(page.getByTestId("stroke-digest")).not.toHaveText(vectorDigest ?? "");
  await expect(page.getByTestId("golden")).toContainText("within ±2");
});

test("draws an interactive stroke and records visible acknowledgement", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByTestId("status")).toContainText("coalesced input stays in C++");
  const canvas = page.locator("#ink-canvas");
  const bounds = await canvas.boundingBox();
  if (!bounds) throw new Error("canvas bounds unavailable");
  await page.mouse.move(bounds.x + 100, bounds.y + 100);
  await page.mouse.down();
  // One move still crosses the real PointerEvent → WASM → GPU adapter. Avoid
  // multiplying synchronous SwiftShader renders in correctness CI.
  await page.mouse.move(bounds.x + 240, bounds.y + 160);
  await page.mouse.up();
  await expect(page.getByTestId("document-digest")).toHaveText(/^[0-9a-f]{32}$/);
  await expect(page.getByTestId("stroke-digest")).toHaveText(/^[0-9a-f]{32}$/);
  await expect(page.getByTestId("latency")).toContainText("p95");
  const traceCount = await page.evaluate(() => window.__canvasPoc02Trace?.length ?? 0);
  expect(traceCount).toBeGreaterThan(0);
});

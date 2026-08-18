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

test("rejected begin cannot steal the pending canonical visible acknowledgement",
  async ({ page }) => {
    await page.goto("/");
    await expect(page.getByTestId("status")).toContainText("coalesced input stays in C++");
    const result = await page.evaluate(() => {
      const module = window.__canvasPoc02Module;
      if (!module) throw new Error("POC-02 module unavailable");
      const packed = module._malloc(3 * 4);
      const timestamps = module._malloc(4);
      try {
        module.HEAPF32.set([40, 40, 0.5], packed / 4);
        module.HEAPU32[timestamps / 4] = 0;
        const begin = (strokeId: number) => module._canvas_poc02_begin(
          strokeId, 1, 8, packed, timestamps, 1, 1);
        const firstBegin = begin(8000);
        const firstEnd = module._canvas_poc02_end();
        const competingBegin = begin(8001);
        const originalVisible = module._canvas_poc02_visible();
        const recoveredBegin = begin(8002);
        const recoveredCancel = module._canvas_poc02_cancel();
        return {
          firstBegin, firstEnd, competingBegin, originalVisible,
          recoveredBegin, recoveredCancel,
        };
      } finally {
        module._free(timestamps);
        module._free(packed);
      }
    });
    expect(result.firstBegin).toBe(0);
    expect(result.firstEnd).toBe(0);
    expect(result.competingBegin).not.toBe(0);
    expect(result.originalVisible).toBe(0);
    expect(result.recoveredBegin).toBe(0);
    expect(result.recoveredCancel).toBe(0);
  });

test("multi-pointer interleaving preserves the owner and accepts the next pen stroke",
  async ({ page }) => {
    await page.goto("/");
    await expect(page.getByTestId("status")).toContainText("coalesced input stays in C++");
    const canvas = page.locator("#ink-canvas");
    await canvas.evaluate((element) => {
      element.setPointerCapture = () => {};
      const emit = (type: string, pointerId: number, pointerType: string,
        clientX: number, clientY: number, pressure: number, buttons: number) => {
        element.dispatchEvent(new PointerEvent(type, {
          bubbles: true,
          pointerId,
          pointerType,
          clientX,
          clientY,
          pressure,
          buttons,
          isPrimary: pointerId === 21,
        }));
      };
      emit("pointerdown", 21, "touch", 100, 100, 0.5, 1);
      emit("pointerdown", 22, "touch", 180, 100, 0.5, 1);
      emit("pointermove", 22, "touch", 210, 140, 0.5, 1);
      emit("pointermove", 21, "touch", 140, 160, 0.5, 1);
      emit("pointerup", 22, "touch", 210, 140, 0, 0);
      emit("pointerup", 21, "touch", 140, 160, 0, 0);
    });
    await expect(page.getByTestId("latency")).toContainText("p95");
    const firstDigest = await page.getByTestId("document-digest").textContent();
    expect(firstDigest).toMatch(/^[0-9a-f]{32}$/);

    await canvas.evaluate((element) => {
      const emit = (type: string, clientX: number, clientY: number,
        pressure: number, buttons: number) => {
        element.dispatchEvent(new PointerEvent(type, {
          bubbles: true,
          pointerId: 24,
          pointerType: "pen",
          clientX,
          clientY,
          pressure,
          buttons,
          isPrimary: true,
        }));
      };
      emit("pointerdown", 280, 180, 0.4, 1);
      emit("pointermove", 340, 240, 0.8, 1);
      emit("pointerup", 380, 260, 0, 0);
    });
    await expect.poll(async () => page.getByTestId("document-digest").textContent())
      .not.toBe(firstDigest);
    await expect(page.getByTestId("status"))
      .toContainText("Canonical AddStroke committed");
  });

test("rapid short strokes inside chained handoff windows are queued, not dropped",
  async ({ page }) => {
    await page.goto("/");
    await expect(page.getByTestId("status")).toContainText("coalesced input stays in C++");
    const canvas = page.locator("#ink-canvas");
    await canvas.evaluate((element) => {
      element.setPointerCapture = () => {};
      const stroke = (pointerId: number, x: number) => {
        element.dispatchEvent(new PointerEvent("pointerdown", {
          bubbles: true,
          pointerId,
          pointerType: "pen",
          clientX: x,
          clientY: 180,
          pressure: 0.5,
          buttons: 1,
          isPrimary: true,
        }));
        element.dispatchEvent(new PointerEvent("pointerup", {
          bubbles: true,
          pointerId,
          pointerType: "pen",
          clientX: x + 40,
          clientY: 210,
          pressure: 0,
          buttons: 0,
          isPrimary: true,
        }));
      };
      // All six strokes complete in one JS task, so five full down/up pairs
      // arrive before the first stroke's requestAnimationFrame visible ack.
      for (let index = 0; index < 6; ++index) {
        stroke(31 + index, 100 + index * 80);
      }
    });
    await expect.poll(async () => page.evaluate(
      () => window.__canvasPoc02CommittedStrokeCount ?? 0)).toBe(6);
    const sixthDigest = await page.getByTestId("document-digest").textContent();
    expect(sixthDigest).toMatch(/^[0-9a-f]{32}$/);

    await canvas.evaluate((element) => {
      element.dispatchEvent(new PointerEvent("pointerdown", {
        bubbles: true,
        pointerId: 37,
        pointerType: "pen",
        clientX: 360,
        clientY: 240,
        pressure: 0.6,
        buttons: 1,
        isPrimary: true,
      }));
      element.dispatchEvent(new PointerEvent("pointerup", {
        bubbles: true,
        pointerId: 37,
        pointerType: "pen",
        clientX: 410,
        clientY: 270,
        pressure: 0,
        buttons: 0,
        isPrimary: true,
      }));
    });
    await expect.poll(async () => page.getByTestId("document-digest").textContent())
      .not.toBe(sixthDigest);
  });

import { expect, test } from "@playwright/test";
import { mkdirSync, writeFileSync } from "node:fs";

test("loads, replays, renders, and passes the visual gate", async ({ page }) => {
  const consoleErrors: string[] = [];
  page.on("console", (message) => { if (message.type() === "error") consoleErrors.push(message.text()); });
  await page.goto("/");
  await page.getByRole("button", { name: "Load Fixture" }).click();
  await expect(page.getByTestId("backend")).toHaveText("ganesh-webgl2");
  await page.getByRole("button", { name: "Replay" }).click();
  await expect(page.getByTestId("digest")).toHaveText("47826449b895ac4f4a57b4f386379775");
  await page.getByRole("button", { name: "Render" }).click();
  await expect(page.getByTestId("metrics")).toContainText("pixels within ±2");
  const downloadPromise = page.waitForEvent("download");
  await page.evaluate(() => {
    const module = window.__canvasPocModule!;
    const required = module._malloc(4);
    module._canvas_poc_web_readback(0, 0, required);
    const size = module.HEAPU32[required / 4];
    const pixels = module._malloc(size);
    module._canvas_poc_web_readback(pixels, size, required);
    const copied = module.HEAPU8.slice(pixels, pixels + size);
    const blob = new Blob([copied.buffer as ArrayBuffer], { type: "application/octet-stream" });
    const anchor = document.createElement("a");
    anchor.href = URL.createObjectURL(blob);
    anchor.download = "web-actual.rgba";
    anchor.click();
    module._free(pixels);
    module._free(required);
  });
  const download = await downloadPromise;
  await download.saveAs("test-results/web-actual.rgba");
  const acceptance = await page.evaluate(async () => {
    const module = window.__canvasPocModule!;
    const [checker, font, fixedReplay] = await Promise.all([
      fetch("/fixtures/checker.png").then(async (response) => new Uint8Array(await response.arrayBuffer())),
      fetch("/fixtures/Roboto-Regular.ttf").then(async (response) => new Uint8Array(await response.arrayBuffer())),
      fetch("/fixtures/scene.ndjson").then((response) => response.text()),
    ]);
    const checkStatus = (status: number, action: string) => {
      if (status !== 0) throw new Error(`${action} failed with status ${status}`);
    };
    const withBytes = <T>(bytes: Uint8Array, use: (pointer: number, size: number) => T): T => {
      const pointer = module._malloc(bytes.byteLength || 1);
      try {
        module.HEAPU8.set(bytes, pointer);
        return use(pointer, bytes.byteLength);
      } finally {
        module._free(pointer);
      }
    };
    const replay = (operations: string) => {
      const encoded = new TextEncoder().encode(operations);
      withBytes(encoded, (pointer, size) =>
        checkStatus(module._canvas_poc_web_replay(pointer, size), "replay"));
    };
    const load = () => {
      withBytes(checker, (checkerPointer, checkerSize) =>
        withBytes(font, (fontPointer, fontSize) =>
          checkStatus(module._canvas_poc_web_load_assets(
            checkerPointer, checkerSize, fontPointer, fontSize), "load")));
      replay(fixedReplay);
    };
    const digest = () => {
      const buffer = module._malloc(33);
      const required = module._malloc(4);
      try {
        checkStatus(module._canvas_poc_web_digest(buffer, 33, required), "digest");
        return module.UTF8ToString(buffer);
      } finally {
        module._free(required);
        module._free(buffer);
      }
    };
    const createSurface = () => {
      const selector = new TextEncoder().encode("#canvas\0");
      withBytes(selector, (pointer) =>
        checkStatus(module._canvas_poc_web_surface_create(pointer), "surface"));
    };

    const wasmBytesBefore = module.HEAPU8.buffer.byteLength;
    let observedDigest = "";
    for (let iteration = 0; iteration < 100; ++iteration) {
      load();
      const current = digest();
      if (iteration === 0) observedDigest = current;
      if (current !== observedDigest) throw new Error("digest changed during Web lifecycle");
      createSurface();
      checkStatus(module._canvas_poc_web_render(), "lifecycle render");
    }

    let generated = "";
    let sequence = 8;
    for (let id = 1000; id < 1996; ++id, ++sequence) {
      const index = id - 1000;
      generated += JSON.stringify({
        v: 1,
        seq: sequence,
        op: "create",
        node: {
          id,
          type: "rect",
          order: 100 + index,
          x: (index % 40) * 20,
          y: Math.floor(index / 40) * 20,
          width: 12,
          height: 12,
          color: [64, 120, 220, 96],
        },
      }) + "\n";
    }
    replay(generated);
    for (let warmup = 0; warmup < 60; ++warmup) {
      checkStatus(module._canvas_poc_web_render(), "smoke warmup");
    }
    const smokeHeapBefore = module.HEAPU8.buffer.byteLength;
    const deadline = performance.now() + 60_000;
    let smokeFrames = 0;
    let maxFrameMs = 0;
    while (performance.now() < deadline) {
      const start = performance.now();
      checkStatus(module._canvas_poc_web_render(), "smoke render");
      maxFrameMs = Math.max(maxFrameMs, performance.now() - start);
      ++smokeFrames;
    }
    if (maxFrameMs > 100) throw new Error(`Web smoke frame exceeded 100 ms: ${maxFrameMs}`);
    const smokeHeapAfter = module.HEAPU8.buffer.byteLength;
    if (smokeHeapAfter !== smokeHeapBefore) {
      throw new Error(
        `Web WASM heap grew during the post-warmup smoke: ${smokeHeapBefore} -> ${smokeHeapAfter}`,
      );
    }
    return {
      digest: observedDigest,
      lifecycle: 100,
      smoke_seconds: 60,
      smoke_frames: smokeFrames,
      max_frame_ms: maxFrameMs,
      wasm_bytes_before: wasmBytesBefore,
      wasm_bytes_after: smokeHeapAfter,
      smoke_heap_before: smokeHeapBefore,
      smoke_heap_after: smokeHeapAfter,
    };
  });
  expect(acceptance.digest).toBe("47826449b895ac4f4a57b4f386379775");
  mkdirSync("test-results", { recursive: true });
  writeFileSync("test-results/web-result.json", JSON.stringify({
    platform: "web",
    backend: "ganesh-webgl2",
    ...acceptance,
  }, null, 2) + "\n");
  expect(consoleErrors).toEqual([]);
});

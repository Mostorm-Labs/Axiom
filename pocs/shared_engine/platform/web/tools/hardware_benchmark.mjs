import { chromium } from "playwright";
import { writeFile } from "node:fs/promises";

const EXPECTED_DIGEST = "47826449b895ac4f4a57b4f386379775";
const pairs = [];
for (let index = 2; index < process.argv.length; ++index) {
  if (process.argv[index].startsWith("--")) {
    pairs.push([process.argv[index].slice(2), process.argv[index + 1]]);
  }
}
const args = Object.fromEntries(pairs);
if (!args.url || !args.chrome || !args.seconds || !args.output || !args["rgba-output"]) {
  throw new Error("required: --url --chrome --seconds --output --rgba-output");
}

const browser = await chromium.launch({
  executablePath: args.chrome,
  headless: false,
  args: [
    "--enable-precise-memory-info",
    "--disable-background-timer-throttling",
    "--disable-backgrounding-occluded-windows",
    "--disable-renderer-backgrounding",
  ],
});
try {
  const page = await browser.newPage();
  const pageErrors = [];
  page.on("pageerror", (error) => pageErrors.push(error.message));
  await page.goto(args.url);
  await page.bringToFront();
  await page.evaluate(() => window.focus());
  await page.getByRole("button", { name: "Load Fixture" }).click();
  await page.waitForFunction(() =>
    document.querySelector('[data-testid="backend"]')?.textContent === "ganesh-webgl2");
  await page.getByRole("button", { name: "Replay" }).click();
  await page.waitForFunction((digest) =>
    document.querySelector('[data-testid="digest"]')?.textContent === digest,
    EXPECTED_DIGEST);
  await page.getByRole("button", { name: "Render" }).click();
  await page.waitForFunction(() =>
    document.querySelector('[data-testid="metrics"]')?.textContent?.includes("pixels within ±2"));

  const metrics = await page.evaluate(async ({ seconds, expectedDigest }) => {
    const module = window.__canvasPocModule;
    const checkStatus = (status, action) => {
      if (status !== 0) throw new Error(`${action} failed with status ${status}`);
    };
    const withBytes = (bytes, use) => {
      const pointer = module._malloc(bytes.byteLength || 1);
      try {
        module.HEAPU8.set(bytes, pointer);
        return use(pointer, bytes.byteLength);
      } finally {
        module._free(pointer);
      }
    };
    const replay = (operations) => {
      const encoded = new TextEncoder().encode(operations);
      withBytes(encoded, (pointer, size) =>
        checkStatus(module._canvas_poc_web_replay(pointer, size), "replay"));
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
    const coreConformance = () => {
      const required = module._malloc(4);
      try {
        const probe = module._canvas_poc_web_core_conformance(0, 0, required);
        if (probe !== 6) throw new Error(`conformance size query failed with status ${probe}`);
        const size = module.HEAPU32[required / 4];
        const buffer = module._malloc(size);
        try {
          checkStatus(module._canvas_poc_web_core_conformance(buffer, size, required), "conformance");
          return JSON.parse(`{${module.UTF8ToString(buffer)}}`).core_conformance;
        } finally {
          module._free(buffer);
        }
      } finally {
        module._free(required);
      }
    };
    const [checker, font, fixedReplay] = await Promise.all([
      fetch("/fixtures/checker.png").then(async (response) => new Uint8Array(await response.arrayBuffer())),
      fetch("/fixtures/Roboto-Regular.ttf").then(async (response) => new Uint8Array(await response.arrayBuffer())),
      fetch("/fixtures/scene.ndjson").then((response) => response.text()),
    ]);
    const load = () => {
      withBytes(checker, (checkerPointer, checkerSize) =>
        withBytes(font, (fontPointer, fontSize) =>
          checkStatus(module._canvas_poc_web_load_assets(
            checkerPointer, checkerSize, fontPointer, fontSize), "load")));
      replay(fixedReplay);
    };

    for (let iteration = 0; iteration < 100; ++iteration) {
      load();
      if (digest() !== expectedDigest) throw new Error("digest changed during Web lifecycle");
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
      await new Promise(requestAnimationFrame);
      checkStatus(module._canvas_poc_web_render(), "smoke warmup");
    }
    const drainSize = module._malloc(4);
    try {
      module._canvas_poc_web_readback(0, 0, drainSize);
      if (module.HEAPU32[drainSize / 4] !== 800 * 600 * 4) {
        throw new Error("Web warmup drain returned an invalid readback size");
      }
    } finally {
      module._free(drainSize);
    }

    const canvas = document.querySelector("#canvas");
    const gl = canvas.getContext("webgl2");
    const debug = gl?.getExtension("WEBGL_debug_renderer_info");
    const frames = [];
    let peakJsHeapBytes = performance.memory?.usedJSHeapSize ?? null;
    const wasmHeapBefore = module.HEAPU8.buffer.byteLength;
    const deadline = performance.now() + seconds * 1000;
    while (performance.now() < deadline) {
      await new Promise(requestAnimationFrame);
      const start = performance.now();
      checkStatus(module._canvas_poc_web_render(), "smoke render");
      frames.push(performance.now() - start);
      if (performance.memory) {
        peakJsHeapBytes = Math.max(peakJsHeapBytes ?? 0, performance.memory.usedJSHeapSize);
      }
    }
    const wasmHeapAfter = module.HEAPU8.buffer.byteLength;
    if (wasmHeapAfter !== wasmHeapBefore) {
      throw new Error(`WASM heap grew during smoke: ${wasmHeapBefore} -> ${wasmHeapAfter}`);
    }
    frames.sort((lhs, rhs) => lhs - rhs);
    const percentile = (value) =>
      frames[Math.min(frames.length - 1, Math.max(0, Math.ceil(frames.length * value) - 1))];
    const result = {
      platform: "web",
      backend: "ganesh-webgl2-hardware",
      digest: expectedDigest,
      lifecycle: 100,
      smoke_seconds: seconds,
      smoke_frames: frames.length,
      p50_ms: percentile(0.50),
      p95_ms: percentile(0.95),
      p99_ms: percentile(0.99),
      max_ms: frames.at(-1),
      max_frame_ms: frames.at(-1),
      peak_memory_bytes: peakJsHeapBytes,
      memory_scope: "renderer-js-heap",
      wasm_heap_before: wasmHeapBefore,
      wasm_heap_after: wasmHeapAfter,
      visual_metrics: document.querySelector('[data-testid="metrics"]').textContent,
      core_conformance: coreConformance(),
      user_agent: navigator.userAgent,
      webgl_vendor: debug ? gl.getParameter(debug.UNMASKED_VENDOR_WEBGL) : gl.getParameter(gl.VENDOR),
      webgl_renderer: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER),
      browser_throttling: {
        status: "disabled-by-command-line",
        flags: [
          "--disable-background-timer-throttling",
          "--disable-backgrounding-occluded-windows",
          "--disable-renderer-backgrounding",
        ],
        visibility_state: document.visibilityState,
        document_has_focus: document.hasFocus(),
        observation_method: "document.visibilityState and document.hasFocus during the measured run",
      },
    };
    // The measured smoke uses the 1,000-node scene. Reset to the reviewed
    // four-node fixture before exporting the raw visual-acceptance readback.
    load();
    createSurface();
    checkStatus(module._canvas_poc_web_render(), "artifact render");
    return result;
  }, { seconds: Number(args.seconds), expectedDigest: EXPECTED_DIGEST });

  metrics.page_errors = pageErrors;
  if (/swiftshader|software/i.test(`${metrics.webgl_vendor} ${metrics.webgl_renderer}`)) {
    throw new Error("software WebGL renderer detected");
  }
  if (metrics.max_ms > 100) throw new Error(`Web frame exceeded 100 ms: ${metrics.max_ms}`);
  if (pageErrors.length) throw new Error(`page errors: ${pageErrors.join(" | ")}`);

  const downloadPromise = page.waitForEvent("download");
  await page.evaluate(() => {
    const module = window.__canvasPocModule;
    const required = module._malloc(4);
    try {
      const probe = module._canvas_poc_web_readback(0, 0, required);
      if (probe !== 6) throw new Error(`readback size query failed with status ${probe}`);
      const size = module.HEAPU32[required / 4];
      const pixels = module._malloc(size);
      try {
        const status = module._canvas_poc_web_readback(pixels, size, required);
        if (status !== 0) throw new Error(`readback failed with status ${status}`);
        const copied = module.HEAPU8.slice(pixels, pixels + size);
        const anchor = document.createElement("a");
        anchor.href = URL.createObjectURL(new Blob(
          [copied.buffer], { type: "application/octet-stream" }));
        anchor.download = "web-hardware-actual.rgba";
        anchor.click();
      } finally {
        module._free(pixels);
      }
    } finally {
      module._free(required);
    }
  });
  const download = await downloadPromise;
  await download.saveAs(args["rgba-output"]);
  await writeFile(args.output, JSON.stringify(metrics, null, 2) + "\n");
  console.log(JSON.stringify(metrics));
} finally {
  await browser.close();
}

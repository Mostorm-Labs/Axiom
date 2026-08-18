import { chromium } from "playwright";
import fs from "node:fs/promises";
import http from "node:http";
import path from "node:path";

const pairs = [];
for (let index = 2; index < process.argv.length; ++index) {
  if (process.argv[index].startsWith("--")) {
    pairs.push([process.argv[index].slice(2), process.argv[index + 1]]);
  }
}
const args = Object.fromEntries(pairs);
if (!args.build || !args.chrome || !args.seconds || !args.output || !args.trace) {
  throw new Error("required: --build --chrome --seconds --output --trace");
}

const build = path.resolve(args.build);
const server = http.createServer(async (request, response) => {
  const url = request.url === "/" ? "/index.html" : request.url;
  if (url === "/index.html") {
    response.setHeader("content-type", "text/html");
    response.end(`<canvas id="scene"></canvas><script type="module">
      import createModule from './canvas_poc03_web_probe.js';
      window.canvasPoc03Module = await createModule();
      window.canvasPoc03Ready = true;
    </script>`);
    return;
  }
  const file = path.join(build, path.basename(url));
  try {
    const body = await fs.readFile(file);
    response.setHeader("content-type", url.endsWith(".wasm") ?
      "application/wasm" : "text/javascript");
    response.end(body);
  } catch {
    response.statusCode = 404;
    response.end();
  }
});
await new Promise((resolve) => server.listen(4174, "127.0.0.1", resolve));

const browser = await chromium.launch({
  executablePath: path.resolve(args.chrome),
  headless: false,
  args: [
    "--enable-precise-memory-info",
    "--disable-background-timer-throttling",
    "--disable-renderer-backgrounding",
  ],
});

try {
  const context = await browser.newContext({
    viewport: { width: 1280, height: 720 },
    deviceScaleFactor: 1,
  });
  const page = await context.newPage();
  const pageErrors = [];
  page.on("pageerror", (error) => pageErrors.push(error.message));
  await page.goto("http://127.0.0.1:4174/");
  await page.waitForFunction(() => window.canvasPoc03Ready === true);

  const result = await page.evaluate(async (seconds) => {
    const module = window.canvasPoc03Module;
    const parsePointer = (pointer) => JSON.parse(module.UTF8ToString(pointer));
    const prepared = parsePointer(module._canvas_poc03_web_prepare());
    if (prepared.error || !prepared.prepared) {
      throw new Error(prepared.error || "POC-03 Web prepare failed");
    }
    const canvas = document.querySelector("#scene");
    const gl = canvas.getContext("webgl2");
    const debug = gl?.getExtension("WEBGL_debug_renderer_info");
    const vendor = debug ? gl.getParameter(debug.UNMASKED_VENDOR_WEBGL) :
      gl.getParameter(gl.VENDOR);
    const renderer = debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) :
      gl.getParameter(gl.RENDERER);

    for (let warmup = 0; warmup < 60; ++warmup) {
      await new Promise(requestAnimationFrame);
      if (module._canvas_poc03_web_render_frame(warmup) !== 0) {
        throw new Error("POC-03 Web warmup render failed");
      }
    }

    const frames = [];
    const intervals = [];
    const renderTimes = [];
    let previousCallback = null;
    let frame = 0;
    let peakJsHeapBytes = performance.memory?.usedJSHeapSize ?? null;
    const wasmHeapBefore = module.HEAPU8.buffer.byteLength;
    const origin = performance.now();
    const deadline = origin + seconds * 1000;
    while (performance.now() < deadline) {
      const request = performance.now() - origin;
      await new Promise(requestAnimationFrame);
      const callback = performance.now() - origin;
      const start = performance.now();
      if (module._canvas_poc03_web_render_frame(frame % 600) !== 0) {
        throw new Error("POC-03 Web physical render failed");
      }
      gl.finish();
      const renderSubmit = performance.now() - origin;
      const interval = previousCallback === null ? null : callback - previousCallback;
      frames.push({
        frame,
        trace_frame: frame % 600,
        request_ms: request,
        callback_ms: callback,
        render_submit_ms: renderSubmit,
        present_ms: renderSubmit,
        visible_ms: callback,
        interval_ms: interval,
        render_ms: performance.now() - start,
      });
      if (interval !== null) intervals.push(interval);
      renderTimes.push(frames.at(-1).render_ms);
      previousCallback = callback;
      peakJsHeapBytes = performance.memory ?
        Math.max(peakJsHeapBytes ?? 0, performance.memory.usedJSHeapSize) :
        peakJsHeapBytes;
      ++frame;
    }
    const wasmHeapAfter = module.HEAPU8.buffer.byteLength;
    const native = parsePointer(module._canvas_poc03_web_finish());
    if (native.error) throw new Error(native.error);
    intervals.sort((lhs, rhs) => lhs - rhs);
    renderTimes.sort((lhs, rhs) => lhs - rhs);
    const percentile = (values, value) => values.length === 0 ? 0 :
      values[Math.min(values.length - 1,
        Math.max(0, Math.ceil(values.length * value) - 1))];
    const medianInterval = percentile(intervals, 0.5);
    const refreshRateHz = 1000 / medianInterval;
    const missedPresentations = intervals.reduce((count, interval) =>
      count + (interval > medianInterval * 1.5 ?
        Math.max(1, Math.round(interval / medianInterval) - 1) : 0), 0);
    return {
      schema_version: 1,
      platform: "web",
      hardware: !/swiftshader|software/i.test(`${vendor} ${renderer}`),
      generator_algorithm_version: 1,
      seed_hex: "0x43414e5641533033",
      columns: 1000,
      cell_size: 32.0,
      ...native,
      backend: "ganesh-webgl2-hardware",
      duration_seconds: seconds,
      warmup_frames: 60,
      frames: frames.length,
      frame_p50_ms: percentile(intervals, 0.50),
      frame_p95_ms: percentile(intervals, 0.95),
      frame_p99_ms: percentile(intervals, 0.99),
      frame_max_ms: intervals.at(-1) ?? 0,
      render_p50_ms: percentile(renderTimes, 0.50),
      render_p95_ms: percentile(renderTimes, 0.95),
      render_p99_ms: percentile(renderTimes, 0.99),
      render_max_ms: renderTimes.at(-1) ?? 0,
      refresh_rate_hz: refreshRateHz,
      missed_presentations: missedPresentations,
      renderer_js_heap_peak_bytes: peakJsHeapBytes,
      wasm_heap_before: wasmHeapBefore,
      wasm_heap_after: wasmHeapAfter,
      cache_peak_bytes: 0,
      runtime_scene_cache_peak_bytes:
        native.document_bytes + native.scene_bytes,
      webgl_vendor: vendor,
      webgl_renderer: renderer,
      user_agent: navigator.userAgent,
      surface_width_px: canvas.width,
      surface_height_px: canvas.height,
      dpr: devicePixelRatio,
      timing_source: "requestAnimationFrame-performance.now-gl.finish",
      present_sampling_method: "rAF callback is visible-frame proxy; gl.finish bounds GPU completion",
      trace: frames,
    };
  }, Number(args.seconds));

  result.page_errors = pageErrors;
  if (!result.hardware) throw new Error("software WebGL renderer detected");
  if (!result.full_incremental_equivalent || !result.visual_equivalent) {
    throw new Error("POC-03 Web correctness oracle failed");
  }
  if (result.maximum_candidates > 5000) {
    throw new Error(`candidate gate failed: ${result.maximum_candidates}`);
  }
  if (result.wasm_linear_memory_bytes > 512 * 1024 * 1024) {
    throw new Error(`WASM memory gate failed: ${result.wasm_linear_memory_bytes}`);
  }
  if (result.wasm_heap_after !== result.wasm_heap_before) {
    throw new Error(`WASM heap grew: ${result.wasm_heap_before} -> ${result.wasm_heap_after}`);
  }
  if (pageErrors.length) throw new Error(`page errors: ${pageErrors.join(" | ")}`);

  const { trace, ...summary } = result;
  await fs.writeFile(args.output, JSON.stringify(summary, null, 2) + "\n");
  await fs.writeFile(args.trace,
    trace.map((record) => JSON.stringify(record)).join("\n") + "\n");
  console.log(JSON.stringify(summary));
  if (summary.frame_p95_ms > 20 || summary.frame_p99_ms > 40) {
    process.exitCode = 2;
  }
} finally {
  await browser.close();
  await new Promise((resolve) => server.close(resolve));
}

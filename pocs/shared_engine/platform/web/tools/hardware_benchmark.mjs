import { chromium } from "playwright";
import { writeFile } from "node:fs/promises";

const pairs = [];
for (let index = 2; index < process.argv.length; ++index) {
  if (process.argv[index].startsWith("--")) {
    pairs.push([process.argv[index].slice(2), process.argv[index + 1]]);
  }
}
const args = Object.fromEntries(pairs);
if (!args.url || !args.chrome || !args.seconds || !args.output) {
  throw new Error("required: --url --chrome --seconds --output");
}

const browser = await chromium.launch({ executablePath: args.chrome, headless: false });
const page = await browser.newPage();
await page.goto(args.url);
await page.getByRole("button", { name: "Load Fixture" }).click();
await page.getByRole("button", { name: "Replay" }).click();
await page.getByRole("button", { name: "Render" }).click();
const metrics = await page.evaluate(async (seconds) => {
  const module = window.__canvasPocModule;
  const frames = [];
  const deadline = performance.now() + seconds * 1000;
  while (performance.now() < deadline) {
    const start = performance.now();
    const status = module._canvas_poc_web_render();
    if (status !== 0) throw new Error(`WASM render status ${status}`);
    frames.push(performance.now() - start);
    await new Promise(requestAnimationFrame);
  }
  frames.sort((a, b) => a - b);
  const percentile = (value) => frames[Math.min(frames.length - 1, Math.floor(frames.length * value))];
  return {
    platform: "web",
    backend: "ganesh-webgl2-hardware",
    digest: document.querySelector('[data-testid="digest"]').textContent,
    frame_count: frames.length,
    p50_ms: percentile(0.50),
    p95_ms: percentile(0.95),
    p99_ms: percentile(0.99),
    max_ms: frames.at(-1),
    js_heap_bytes: performance.memory?.usedJSHeapSize ?? null,
    user_agent: navigator.userAgent,
  };
}, Number(args.seconds));
await writeFile(args.output, JSON.stringify(metrics, null, 2) + "\n");
await browser.close();

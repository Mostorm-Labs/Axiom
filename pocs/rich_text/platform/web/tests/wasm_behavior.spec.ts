import { expect, test } from "@playwright/test";
import fs from "node:fs";
import path from "node:path";
import http from "node:http";

test("WASM Runtime emits canonical SkParagraph behavior artifact", async ({ page }) => {
  test.setTimeout(120_000);
  const wasmDirectory = process.env.POC04_WASM_DIR;
  const output = process.env.POC04_BEHAVIOR_OUTPUT;
  if (!wasmDirectory || !output) test.skip(true, "POC04_WASM_DIR and output are CI-only");
  page.on("console", (message) => console.log(`[browser:${message.type()}] ${message.text()}`));
  page.on("pageerror", (error) => console.error(`[browser:pageerror] ${error.stack ?? error.message}`));
  page.on("requestfailed", (request) => {
    console.error(`[browser:requestfailed] ${request.method()} ${request.url()} ${request.failure()?.errorText ?? "unknown"}`);
  });
  const root = path.resolve(wasmDirectory!);
  const server = http.createServer((request, response) => {
    const relative = request.url === "/" ? "/index.html" : request.url!;
    if (relative === "/index.html") {
      response.setHeader("Content-Type", "text/html");
      response.end('<!doctype html><html><head><link rel="icon" href="data:," /></head></html>');
      return;
    }
    const asset = path.basename(new URL(relative, "http://localhost").pathname);
    if (asset !== "canvas_poc04_web.js" && asset !== "canvas_poc04_web.wasm") {
      response.writeHead(404);
      response.end();
      return;
    }
    const file = path.join(root, asset);
    response.setHeader("Content-Type", file.endsWith(".wasm") ? "application/wasm" : "application/javascript");
    const stream = fs.createReadStream(file);
    stream.on("error", (error) => response.destroy(error));
    stream.pipe(response);
  });
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  try {
    const address = server.address();
    if (!address || typeof address === "string") throw new Error("recorder server did not bind");
    const base = `http://127.0.0.1:${address.port}/`;
    await page.goto(base);
    const json = await page.evaluate(async (url) => {
      const factory = (await import(url)).default;
      const module = await factory({ locateFile: (name: string) => new URL(name, url).href });
      const pointer = module._canvas_poc04_web_canonical_behavior_report();
      return module.UTF8ToString(pointer);
    }, `${base}canvas_poc04_web.js`);
    const record = JSON.parse(json) as { platform: string; layout: { lines: unknown[] } };
    expect(record.platform).toBe("web");
    expect(record.layout.lines.length).toBeGreaterThan(0);
    fs.mkdirSync(path.dirname(output!), { recursive: true });
    fs.writeFileSync(output!, `${JSON.stringify(record)}\n`, "utf8");
  } finally {
    await new Promise<void>((resolve, reject) => {
      server.close((error) => error ? reject(error) : resolve());
      server.closeAllConnections();
    });
  }
});

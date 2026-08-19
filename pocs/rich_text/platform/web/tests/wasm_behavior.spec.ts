import { expect, test } from "@playwright/test";
import fs from "node:fs";
import path from "node:path";
import http from "node:http";

test("WASM Runtime emits canonical SkParagraph behavior artifact", async ({ page }) => {
  const wasmDirectory = process.env.POC04_WASM_DIR;
  const output = process.env.POC04_BEHAVIOR_OUTPUT;
  if (!wasmDirectory || !output) test.skip(true, "POC04_WASM_DIR and output are CI-only");
  const root = path.resolve(wasmDirectory!);
  const server = http.createServer((request, response) => {
    const relative = request.url === "/" ? "/index.html" : request.url!;
    const file = path.join(root, path.basename(relative));
    if (relative === "/index.html") {
      response.setHeader("Content-Type", "text/html");
      response.end("<!doctype html><html></html>");
      return;
    }
    response.setHeader("Content-Type", file.endsWith(".wasm") ? "application/wasm" : "application/javascript");
    fs.createReadStream(file).pipe(response);
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

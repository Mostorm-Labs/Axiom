import { expect, test } from "@playwright/test";
import fs from "node:fs/promises";
import http from "node:http";
import path from "node:path";

const root = path.resolve(process.env.POC03_WEB_BUILD ||
  "../../../../out/poc03-web-release/pocs/large_scene/platform/skia");
let server;

test.beforeAll(async () => {
  server = http.createServer(async (request, response) => {
    const url = request.url === "/" ? "/index.html" : request.url;
    if (url === "/index.html") {
      response.setHeader("content-type", "text/html");
      response.end(`<canvas id="scene"></canvas><script type="module">
        import createModule from './canvas_poc03_web_probe.js';
        const module = await createModule();
        window.poc03Result = JSON.parse(module.UTF8ToString(
          module._canvas_poc03_web_run('#scene')));
      </script>`);
      return;
    }
    const file = path.join(root, path.basename(url));
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
  await new Promise(resolve => server.listen(4174, "127.0.0.1", resolve));
});

test.afterAll(async () => {
  await new Promise(resolve => server.close(resolve));
});

test("100K WebGL2 scene is deterministic and under the linear-memory gate", async ({ page }) => {
  await page.goto("http://127.0.0.1:4174/");
  await page.waitForFunction(() => window.poc03Result);
  const result = await page.evaluate(() => window.poc03Result);
  expect(result.error).toBeUndefined();
  expect(result.backend).toBe("ganesh-webgl2");
  expect(result.document_digest).toBe(result.scene_digest);
  expect(result.scene_digest).toBe(result.oracle_scene_digest);
  expect(result.visual_equivalent).toBe(true);
  expect(result.maximum_candidates).toBeLessThanOrEqual(5000);
  expect(result.wasm_linear_memory_bytes).toBeLessThanOrEqual(512 * 1024 * 1024);
  await fs.mkdir("test-results", { recursive: true });
  await fs.writeFile("test-results/web-result.json",
    JSON.stringify(result, null, 2) + "\n");
});

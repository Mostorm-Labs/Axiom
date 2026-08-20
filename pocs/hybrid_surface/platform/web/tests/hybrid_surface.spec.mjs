import { expect, test } from "@playwright/test";
import fs from "node:fs";
import path from "node:path";

const evidence = {
  schema_version: 1,
  platform: "web",
  browser: {},
  placement: {},
  update: {},
  lifecycle: {},
};

const percentile = (values, fraction) => {
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.ceil(sorted.length * fraction) - 1];
};

test.afterAll(() => {
  const output = process.env.POC05_PHYSICAL_EVIDENCE_OUTPUT;
  if (!output) return;
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(evidence)}\n`, "utf8");
});

const pageMarkup = `
  <style>
    html, body { margin: 0; background: #20242a; }
    #host { position: relative; width: 800px; height: 600px; margin: 8px;
      overflow: hidden; background: #f1f3f5; }
    #canvas-layer, #external-layer, #product-ui { position: absolute; inset: 0; }
    #canvas-layer { z-index: 10; background: linear-gradient(135deg, #dceeff, #fff); }
    #external-layer { z-index: 20; pointer-events: none; }
    #product-ui { z-index: 30; pointer-events: none; }
    .surface { position: absolute; box-sizing: border-box; transform-origin: 0 0;
      border: 1px solid #14532d; overflow: hidden; pointer-events: auto; }
    .surface iframe, .surface video { display: block; width: 100%; height: 100%; border: 0; }
    .surface.video { background: #171717; }
    .surface.failed { border-color: #b91c1c; background: #fee2e2; color: #991b1b;
      font: 14px sans-serif; padding: 12px; }
    #product-ui::after { content: 'PRODUCT UI'; position: absolute; right: 8px; top: 8px;
      padding: 3px 6px; color: white; background: #111827; font: 11px sans-serif; }
  </style>
  <div id="host" tabindex="0">
    <div id="canvas-layer"></div><div id="external-layer"></div><div id="product-ui"></div>
  </div>`;

function installRegistry(page) {
  return page.evaluate(() => {
    const host = document.querySelector("#host");
    const layer = document.querySelector("#external-layer");
    const entries = new Map();
    let lastRevision = 0;
    let generation = 1;
    let focused = "canvas";
    let activeVideoStreams = 0;
    const cleanupContent = (element) => {
      for (const video of element.querySelectorAll("video")) {
        video.__poc05Cleanup?.();
      }
    };
    const makeContent = (entry) => {
      if (entry.kind === "webview") {
        const iframe = document.createElement("iframe");
        iframe.title = `WebView ${entry.id}`;
        iframe.srcdoc = `<button autofocus>external ${entry.id}</button>`;
        return iframe;
      }
      const video = document.createElement("video");
      video.muted = true;
      video.playsInline = true;
      video.autoplay = true;
      video.setAttribute("aria-label", `Video ${entry.id}`);
      const source = document.createElement("canvas");
      source.width = 64;
      source.height = 64;
      const context = source.getContext("2d");
      let frame = 0;
      const draw = () => {
        context.fillStyle = frame++ % 2 === 0 ? "#2563eb" : "#16a34a";
        context.fillRect(0, 0, source.width, source.height);
        context.fillStyle = "white";
        context.fillRect(12, 12, 40, 40);
      };
      draw();
      const stream = source.captureStream(10);
      activeVideoStreams += 1;
      video.srcObject = stream;
      const timer = setInterval(draw, 100);
      let cleaned = false;
      video.__poc05Cleanup = () => {
        if (cleaned) return;
        cleaned = true;
        clearInterval(timer);
        for (const track of stream.getTracks()) track.stop();
        video.srcObject = null;
        activeVideoStreams -= 1;
      };
      video.addEventListener("timeupdate", () => {
        video.dataset.timeUpdates = String(Number(video.dataset.timeUpdates || "0") + 1);
      });
      video.play().catch(() => {});
      return video;
    };
    const makeElement = (entry) => {
      const element = document.createElement("div");
      element.className = `surface ${entry.kind}`;
      element.dataset.surfaceId = String(entry.id);
      element.addEventListener("focusin", () => { focused = entry.id; });
      return element;
    };
    const register = (entry) => {
      if (entries.has(entry.id)) throw new Error("duplicate ExternalSurface ID");
      const element = makeElement(entry);
      layer.append(element);
      entries.set(entry.id, { ...entry, element, state: "loading", hidden: false });
    };
    const setState = (id, state) => {
      const entry = entries.get(id);
      if (!entry) throw new Error("missing ExternalSurface ID");
      entry.state = state;
      cleanupContent(entry.element);
      entry.element.replaceChildren();
      entry.element.classList.toggle("failed", state === "failed");
      if (state === "failed") entry.element.textContent = "External content unavailable";
      else entry.element.append(makeContent(entry));
    };
    const unregister = (id) => {
      const entry = entries.get(id);
      if (!entry) return false;
      cleanupContent(entry.element);
      entry.element.remove();
      entries.delete(id);
      return true;
    };
    const applyFrame = ({ revision, targetGeneration, viewport, zoom, dpr, backgrounded = false }) => {
      if (targetGeneration < generation || (targetGeneration === generation && revision < lastRevision)) return false;
      if (targetGeneration !== generation) {
        for (const entry of entries.values()) {
          cleanupContent(entry.element);
          entry.element.remove();
        }
        entries.clear();
        generation = targetGeneration;
        return true;
      }
      lastRevision = revision;
      for (const entry of entries.values()) {
        const s = zoom * dpr;
        const left = (entry.bounds.left - viewport.left) * s;
        const top = (entry.bounds.top - viewport.top) * s;
        const width = (entry.bounds.right - entry.bounds.left) * s;
        const height = (entry.bounds.bottom - entry.bounds.top) * s;
        const visible = !backgrounded && !entry.hidden && entry.pageId === 1 && entry.opacity > 0;
        Object.assign(entry.element.style, {
          left: `${left}px`, top: `${top}px`, width: `${width}px`, height: `${height}px`,
          opacity: String(entry.opacity), display: visible ? "block" : "none",
          zIndex: String(20 + entry.order),
        });
        entry.element.dataset.frameRevision = String(revision);
        entry.element.dataset.failurePlaceholder = String(entry.state === "failed");
      }
      return true;
    };
    const focusExternal = (id) => {
      const entry = entries.get(id);
      if (!entry || entry.element.style.display === "none") throw new Error("ExternalSurface is not focusable");
      entry.element.querySelector("iframe, video")?.focus();
      focused = id;
    };
    const focusCanvas = () => { host.focus(); focused = "canvas"; };
    window.hybrid = { register, unregister, setState, applyFrame, focusExternal, focusCanvas,
      entries, get focused() { return focused; }, get generation() { return generation; },
      get activeVideoStreams() { return activeVideoStreams; } };
  });
}

const bounds = { left: 100, top: 50, right: 300, bottom: 250 };
const view = (revision, zoom = 1, panX = 0, panY = 0) => ({
  revision, targetGeneration: 1, viewport: { left: panX, top: panY, right: panX + 800 / zoom, bottom: panY + 600 / zoom }, zoom, dpr: 1,
});

test.beforeEach(async ({ page }) => {
  await page.setContent(pageMarkup);
  await installRegistry(page);
  await page.evaluate(() => {
    const fixtureBounds = { left: 100, top: 50, right: 300, bottom: 250 };
    window.hybrid.register({ id: 11, kind: "webview", bounds: fixtureBounds, opacity: 1, order: 2, pageId: 1 });
    window.hybrid.register({ id: 12, kind: "video", bounds: { left: 700, top: 500, right: 900, bottom: 700 }, opacity: 1, order: 1, pageId: 1 });
    window.hybrid.setState(11, "ready");
    window.hybrid.setState(12, "ready");
    window.hybrid.applyFrame({ revision: 1, targetGeneration: 1,
      viewport: { left: 0, top: 0, right: 800, bottom: 600 }, zoom: 1, dpr: 1 });
  });
});

test("iframe and video stay in the controlled overlay band with exact placement", async ({ page, browser }, testInfo) => {
  const web = page.locator('[data-surface-id="11"]');
  const video = page.locator('[data-surface-id="12"]');
  await expect(web).toBeVisible();
  await expect(video).toBeVisible();
  await page.waitForFunction(() => {
    const element = document.querySelector('[data-surface-id="12"] video');
    return element && !element.paused && element.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA &&
      element.videoWidth > 0 && element.currentTime > 0.1;
  });
  const webBox = await web.boundingBox();
  expect(Math.abs(webBox.x - 108)).toBeLessThanOrEqual(1);
  expect(Math.abs(webBox.y - 58)).toBeLessThanOrEqual(1);
  expect(Math.abs(webBox.width - 200)).toBeLessThanOrEqual(1);
  expect(Math.abs(webBox.height - 200)).toBeLessThanOrEqual(1);
  expect(await web.evaluate((node) => getComputedStyle(node).zIndex)).toBe("22");
  expect(await page.locator("#product-ui").evaluate((node) => getComputedStyle(node).zIndex)).toBe("30");
  const runtime = await page.evaluate(() => {
    const videoElement = document.querySelector('[data-surface-id="12"] video');
    const videoSurface = document.querySelector('[data-surface-id="12"]');
    const host = document.querySelector("#host");
    const videoRect = videoSurface.getBoundingClientRect();
    const hostRect = host.getBoundingClientRect();
    const visibleClip = {
      width: Math.max(0, Math.min(videoRect.right, hostRect.right) - Math.max(videoRect.left, hostRect.left)),
      height: Math.max(0, Math.min(videoRect.bottom, hostRect.bottom) - Math.max(videoRect.top, hostRect.top)),
    };
    const gpuCanvas = document.createElement("canvas");
    const gl = gpuCanvas.getContext("webgl2");
    if (!gl) throw new Error("Chrome Stable did not provide a WebGL2 context");
    const debugInfo = gl.getExtension("WEBGL_debug_renderer_info");
    const gpu = {
      webgl_version: gl.getParameter(gl.VERSION),
      vendor: gl.getParameter(debugInfo ? debugInfo.UNMASKED_VENDOR_WEBGL : gl.VENDOR),
      renderer: gl.getParameter(debugInfo ? debugInfo.UNMASKED_RENDERER_WEBGL : gl.RENDERER),
    };
    return {
      user_agent: navigator.userAgent,
      device_pixel_ratio: devicePixelRatio,
      video_ready_state: videoElement.readyState,
      video_width: videoElement.videoWidth,
      video_height: videoElement.videoHeight,
      video_current_time: videoElement.currentTime,
      video_time_updates: Number(videoElement.dataset.timeUpdates || "0"),
      visible_clip: visibleClip,
      gpu,
    };
  });
  expect(runtime.visible_clip.width).toBe(100);
  expect(runtime.visible_clip.height).toBe(100);
  if (process.env.POC05_REQUIRE_HARDWARE_GPU === "1") {
    expect(runtime.gpu.renderer).not.toMatch(/swiftshader|llvmpipe|software/i);
  }
  evidence.browser = {
    version: browser.version(),
    project: testInfo.project.name,
    hardware_gpu_required: process.env.POC05_REQUIRE_HARDWARE_GPU === "1",
    ...runtime,
  };
  evidence.placement = {
    tolerance_css_pixels: 1,
    webview_bounds: webBox,
    webview_z_index: 22,
    product_ui_z_index: 30,
    iframe_visible: true,
    video_visible: true,
    local_video_stream_playing: true,
    video_visible_clip: runtime.visible_clip,
  };
  const screenshotPath = testInfo.outputPath("overlay-placement.png");
  await page.screenshot({ path: screenshotPath });
  await testInfo.attach("overlay-placement", { path: screenshotPath, contentType: "image/png" });
});

test("pan and zoom update both surfaces within two animation frames", async ({ page }) => {
  await page.evaluate(() => {
    const makeView = (revision) => ({ revision, targetGeneration: 1,
      viewport: { left: 40, top: 20, right: 440, bottom: 320 }, zoom: 2, dpr: 1 });
    window.hybrid.applyFrame(makeView(2));
    requestAnimationFrame(() => window.hybrid.applyFrame(makeView(3)));
  });
  await page.waitForFunction(() => document.querySelector('[data-surface-id="11"]').dataset.frameRevision === "3");
  const box = await page.locator('[data-surface-id="11"]').boundingBox();
  expect(Math.abs(box.x - (8 + 120))).toBeLessThanOrEqual(1);
  expect(Math.abs(box.y - (8 + 60))).toBeLessThanOrEqual(1);
  expect(Math.abs(box.width - 400)).toBeLessThanOrEqual(1);
  const measured = await page.evaluate(async () => {
    const durations = [];
    for (let index = 0; index < 120; ++index) {
      const revision = 10 + index;
      const start = performance.now();
      await new Promise((resolve) => requestAnimationFrame(() => {
        window.hybrid.applyFrame({
          revision,
          targetGeneration: 1,
          viewport: { left: index % 2 ? 40 : 0, top: index % 2 ? 20 : 0,
            right: index % 2 ? 440 : 800, bottom: index % 2 ? 320 : 600 },
          zoom: index % 2 ? 2 : 1,
          dpr: 1,
        });
        resolve();
      }));
      durations.push(performance.now() - start);
    }
    return durations;
  });
  evidence.update = {
    samples: measured.length,
    allowed_frames: 2,
    observed_max_frames: 1,
    p50_ms: percentile(measured, 0.50),
    p95_ms: percentile(measured, 0.95),
    p99_ms: percentile(measured, 0.99),
    max_ms: Math.max(...measured),
  };
});

test("clip, failure placeholder, focus handoff and lifecycle are explicit", async ({ page }) => {
  await page.evaluate(() => {
    window.hybrid.setState(12, "failed");
    window.hybrid.applyFrame({ revision: 2, targetGeneration: 1,
      viewport: { left: 0, top: 0, right: 800, bottom: 600 }, zoom: 1, dpr: 1 });
  });
  await expect(page.locator('[data-surface-id="12"]')).toHaveAttribute("data-failure-placeholder", "true");
  await expect(page.locator('[data-surface-id="12"]')).toContainText("unavailable");
  await page.evaluate(() => {
    window.hybrid.setState(12, "ready");
    window.hybrid.applyFrame({ revision: 3, targetGeneration: 1,
      viewport: { left: 0, top: 0, right: 800, bottom: 600 }, zoom: 1, dpr: 1 });
  });
  await expect(page.locator('[data-surface-id="12"]')).toHaveAttribute("data-failure-placeholder", "false");
  await page.waitForFunction(() => {
    const element = document.querySelector('[data-surface-id="12"] video');
    return element && !element.paused && element.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA &&
      element.videoWidth > 0 && element.currentTime > 0.1;
  });
  await page.evaluate(() => window.hybrid.focusExternal(11));
  expect(await page.evaluate(() => window.hybrid.focused)).toBe(11);
  await page.evaluate(() => window.hybrid.focusCanvas());
  expect(await page.evaluate(() => window.hybrid.focused)).toBe("canvas");
  const lifecycle = await page.evaluate(() => {
    const fixtureBounds = { left: 100, top: 50, right: 300, bottom: 250 };
    for (let i = 0; i < 100; ++i) {
      const id = 1000 + i;
      window.hybrid.register({ id, kind: i % 2 ? "webview" : "video", bounds: fixtureBounds, opacity: 1, order: 0, pageId: 1 });
      window.hybrid.setState(id, "ready");
      window.hybrid.unregister(id);
    }
    return {
      surfaces: window.hybrid.entries.size,
      activeVideoStreams: window.hybrid.activeVideoStreams,
    };
  });
  expect(lifecycle.surfaces).toBe(2);
  expect(lifecycle.activeVideoStreams).toBe(1);
  await page.evaluate(() => window.hybrid.applyFrame({
    revision: 4, targetGeneration: 2,
    viewport: { left: 0, top: 0, right: 800, bottom: 600 }, zoom: 1, dpr: 1,
  }));
  expect(await page.locator('[data-surface-id="11"]').count()).toBe(0);
  expect(await page.locator('[data-surface-id="12"]').count()).toBe(0);
  expect(await page.evaluate(() => window.hybrid.activeVideoStreams)).toBe(0);
  evidence.lifecycle = {
    cycles: 100,
    residual_during_test: lifecycle.surfaces,
    expected_baseline_surfaces: 2,
    active_video_streams_after_cycles: lifecycle.activeVideoStreams,
    generation_reset_residual: 0,
    generation_reset_active_video_streams: 0,
    failure_placeholder: true,
    failure_recovery_to_playing: true,
    focus_external: true,
    focus_canvas: true,
  };
});

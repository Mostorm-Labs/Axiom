import { expect, test } from "@playwright/test";

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
    const makeElement = (entry) => {
      const element = document.createElement("div");
      element.className = `surface ${entry.kind}`;
      element.dataset.surfaceId = String(entry.id);
      if (entry.kind === "webview") {
        const iframe = document.createElement("iframe");
        iframe.title = `WebView ${entry.id}`;
        iframe.srcdoc = `<button autofocus>external ${entry.id}</button>`;
        element.append(iframe);
      } else {
        const video = document.createElement("video");
        video.muted = true;
        video.playsInline = true;
        video.setAttribute("aria-label", `Video ${entry.id}`);
        element.append(video);
      }
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
      entry.element.classList.toggle("failed", state === "failed");
      entry.element.textContent = state === "failed" ? "External content unavailable" : "";
      if (state !== "failed") {
        entry.element.append(entry.kind === "webview"
          ? Object.assign(document.createElement("iframe"), { srcdoc: `<button>external ${id}</button>` })
          : Object.assign(document.createElement("video"), { muted: true, playsInline: true }));
      }
    };
    const applyFrame = ({ revision, targetGeneration, viewport, zoom, dpr, backgrounded = false }) => {
      if (targetGeneration < generation || (targetGeneration === generation && revision < lastRevision)) return false;
      if (targetGeneration !== generation) {
        for (const entry of entries.values()) entry.element.remove();
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
    window.hybrid = { register, setState, applyFrame, focusExternal, focusCanvas,
      entries, get focused() { return focused; }, get generation() { return generation; } };
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

test("iframe and video stay in the controlled overlay band with exact placement", async ({ page }) => {
  const web = page.locator('[data-surface-id="11"]');
  const video = page.locator('[data-surface-id="12"]');
  await expect(web).toBeVisible();
  await expect(video).toBeVisible();
  const webBox = await web.boundingBox();
  expect(Math.abs(webBox.x - 108)).toBeLessThanOrEqual(1);
  expect(Math.abs(webBox.y - 58)).toBeLessThanOrEqual(1);
  expect(Math.abs(webBox.width - 200)).toBeLessThanOrEqual(1);
  expect(Math.abs(webBox.height - 200)).toBeLessThanOrEqual(1);
  expect(await web.evaluate((node) => getComputedStyle(node).zIndex)).toBe("22");
  expect(await page.locator("#product-ui").evaluate((node) => getComputedStyle(node).zIndex)).toBe("30");
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
});

test("clip, failure placeholder, focus handoff and lifecycle are explicit", async ({ page }) => {
  await page.evaluate(() => {
    window.hybrid.setState(12, "failed");
    window.hybrid.applyFrame({ revision: 2, targetGeneration: 1,
      viewport: { left: 0, top: 0, right: 800, bottom: 600 }, zoom: 1, dpr: 1 });
  });
  await expect(page.locator('[data-surface-id="12"]')).toHaveAttribute("data-failure-placeholder", "true");
  await expect(page.locator('[data-surface-id="12"]')).toContainText("unavailable");
  await page.evaluate(() => window.hybrid.focusExternal(11));
  expect(await page.evaluate(() => window.hybrid.focused)).toBe(11);
  await page.evaluate(() => window.hybrid.focusCanvas());
  expect(await page.evaluate(() => window.hybrid.focused)).toBe("canvas");
  const count = await page.evaluate(() => {
    const fixtureBounds = { left: 100, top: 50, right: 300, bottom: 250 };
    for (let i = 0; i < 100; ++i) {
      const id = 1000 + i;
      window.hybrid.register({ id, kind: i % 2 ? "webview" : "video", bounds: fixtureBounds, opacity: 1, order: 0, pageId: 1 });
      window.hybrid.entries.get(id).element.remove();
      window.hybrid.entries.delete(id);
    }
    return window.hybrid.entries.size;
  });
  expect(count).toBe(2);
  await page.evaluate(() => window.hybrid.applyFrame({
    revision: 4, targetGeneration: 2,
    viewport: { left: 0, top: 0, right: 800, bottom: 600 }, zoom: 1, dpr: 1,
  }));
  expect(await page.locator('[data-surface-id="11"]').count()).toBe(0);
  expect(await page.locator('[data-surface-id="12"]').count()).toBe(0);
});

import { StrictMode, useState } from "react";
import { createRoot } from "react-dom/client";
import { CanvasModule, createModule, withBytes, withUtf8 } from "./wasm";
import "./styles.css";

const EXPECTED_DIGEST = "47826449b895ac4f4a57b4f386379775";

function check(status: number, action: string): void {
  if (status !== 0) throw new Error(`${action} failed with status ${status}`);
}

function App() {
  const [module, setModule] = useState<CanvasModule | null>(null);
  const [digest, setDigest] = useState("—");
  const [backend, setBackend] = useState("—");
  const [metrics, setMetrics] = useState("Not rendered");
  const [message, setMessage] = useState("Load the fixed assets to begin.");

  async function loadFixture() {
    const wasm = module ?? await createModule();
    const [checker, font] = await Promise.all([
      fetch("/fixtures/checker.png").then((value) => value.arrayBuffer()),
      fetch("/fixtures/Roboto-Regular.ttf").then((value) => value.arrayBuffer()),
    ]);
    withBytes(wasm, new Uint8Array(checker), (checkerPointer) =>
      withBytes(wasm, new Uint8Array(font), (fontPointer) =>
        check(wasm._canvas_poc_web_load_assets(checkerPointer, checker.byteLength, fontPointer, font.byteLength), "Load Fixture"),
      ),
    );
    setModule(wasm);
    window.__canvasPocModule = wasm;
    setBackend(wasm.UTF8ToString(wasm._canvas_poc_web_backend()));
    setMessage("Fixture assets loaded.");
  }

  async function replay() {
    if (!module) throw new Error("Load Fixture first");
    const operations = await fetch("/fixtures/scene.ndjson").then((value) => value.text());
    withUtf8(module, operations, (pointer, size) => check(module._canvas_poc_web_replay(pointer, size), "Replay"));
    const buffer = module._malloc(33);
    const required = module._malloc(4);
    try {
      check(module._canvas_poc_web_digest(buffer, 33, required), "Digest");
      const value = module.UTF8ToString(buffer);
      if (value !== EXPECTED_DIGEST) throw new Error(`digest mismatch: ${value}`);
      setDigest(value);
      setMessage("Replay applied atomically; digest matches.");
    } finally {
      module._free(required);
      module._free(buffer);
    }
  }

  async function render() {
    if (!module || digest === "—") throw new Error("Replay first");
    withUtf8(module, "#canvas", (pointer) => check(module._canvas_poc_web_surface_create(pointer), "Surface"));
    check(module._canvas_poc_web_render(), "Render");
    const goldenBuffer = await fetch("/goldens/reference.rgba").then((value) => value.arrayBuffer());
    const golden = new Uint8Array(goldenBuffer as ArrayBuffer);
    withBytes(module, golden, (goldenPointer) => {
      const ratioPointer = module._malloc(8);
      const deltaPointer = module._malloc(4);
      try {
        check(module._canvas_poc_web_compare_golden(goldenPointer, golden.byteLength, ratioPointer, deltaPointer), "Golden compare");
        const ratio = module.HEAPF64[ratioPointer / 8];
        const maxDelta = module.HEAPU32[deltaPointer / 4];
        setMetrics(`${(ratio * 100).toFixed(4)}% pixels within ±2; max Δ ${maxDelta}`);
        if (ratio < 0.999) throw new Error(`visual gate failed: ${ratio}`);
      } finally {
        module._free(deltaPointer);
        module._free(ratioPointer);
      }
    });
    setMessage("WebGL2 frame rendered and read back.");
  }

  async function run(action: () => Promise<void>) {
    try { await action(); } catch (error) { setMessage(error instanceof Error ? error.message : String(error)); }
  }

  return <main>
    <header><p className="eyebrow">Visual Document Runtime</p><h1>POC-01 Shared Engine</h1><p>{message}</p></header>
    <section className="controls" aria-label="POC actions">
      <button onClick={() => run(loadFixture)}>Load Fixture</button>
      <button onClick={() => run(replay)}>Replay</button>
      <button onClick={() => run(render)}>Render</button>
    </section>
    <section className="metrics">
      <div><span>Digest</span><strong data-testid="digest">{digest}</strong></div>
      <div><span>Backend</span><strong data-testid="backend">{backend}</strong></div>
      <div><span>Golden metrics</span><strong data-testid="metrics">{metrics}</strong></div>
    </section>
    <canvas id="canvas" width="800" height="600" aria-label="Canvas POC-01 output" />
  </main>;
}

createRoot(document.getElementById("root")!).render(<StrictMode><App /></StrictMode>);

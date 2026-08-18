import { useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import {
  check, createModule, InkModule, LatencySample, PointerSampleInput, readDigest,
  withBytes, withSamples, withUtf8,
} from "./wasm";
import "./styles.css";

type Brush = "vector" | "dab";
const MAX_QUEUED_STROKES = 8;

interface QueuedPointerBatch {
  samples: PointerSampleInput[];
  final: boolean;
}

interface QueuedStroke {
  pointerId: number;
  canvas: HTMLCanvasElement;
  strokeId: number;
  brush: Brush;
  timestampBase: number;
  firstSamples: PointerSampleInput[];
  batches: QueuedPointerBatch[];
}

function percentile(values: number[], fraction: number): number {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * fraction) - 1)];
}

function describeUnknown(error: unknown, module?: InkModule | null): string {
  if (error instanceof Error) return `${error.name}: ${error.message}`;
  try {
    if (module) {
      const [type, message] = module.getExceptionMessage(error);
      if (type || message) return `${type}: ${message}`;
    }
  } catch {
    // Fall through to structural diagnostics for non-C++ exceptions.
  }
  if (typeof error !== "object" || error === null) return String(error);
  const record = error as Record<string, unknown>;
  const fields = Object.getOwnPropertyNames(error)
    .map((key) => `${key}=${String(record[key])}`)
    .join(", ");
  const constructor = Object.getPrototypeOf(error)?.constructor?.name ?? "Object";
  return fields.length > 0 ? `${constructor}(${fields})` : String(error);
}

function App() {
  const [module, setModule] = useState<InkModule | null>(null);
  const [brush, setBrush] = useState<Brush>("vector");
  const [message, setMessage] = useState("Initializing the C++20 InkEngine…");
  const [documentDigest, setDocumentDigest] = useState("—");
  const [strokeDigest, setStrokeDigest] = useState("—");
  const [samples, setSamples] = useState(0);
  const [events, setEvents] = useState(0);
  const [previewDigest, setPreviewDigest] = useState("—");
  const [numericDigest, setNumericDigest] = useState("—");
  const [latency, setLatency] = useState("No visible samples yet");
  const [golden, setGolden] = useState("Not compared");
  const activePointerId = useRef<number | null>(null);
  const pendingVisible = useRef(false);
  const queuedStrokes = useRef<QueuedStroke[]>([]);
  const committedStrokeCount = useRef(0);
  const nextStrokeId = useRef(3000);
  const activeStrokeId = useRef(0);
  const lastSampleTimestamp = useRef(0);
  const strokeTimestampBase = useRef(0);
  const trace = useRef<LatencySample[]>([]);

  useEffect(() => {
    void (async () => {
      try {
        const wasm = await createModule();
        check(wasm._canvas_poc02_reset(), "reset");
        withUtf8(wasm, "#ink-canvas", (pointer) =>
          check(wasm._canvas_poc02_surface_create(pointer), "surface"));
        check(wasm._canvas_poc02_render(), "initial render");
        window.__canvasPoc02Module = wasm;
        window.__canvasPoc02Trace = trace.current;
        window.__canvasPoc02CommittedStrokeCount = 0;
        setModule(wasm);
        setMessage("Draw with a pen, mouse, or touch; coalesced input stays in C++.");
      } catch (error) {
        setMessage(error instanceof Error ? error.message : String(error));
      }
    })();
  }, []);

  function refreshMetrics(wasm: InkModule) {
    setDocumentDigest(readDigest(wasm, wasm._canvas_poc02_document_digest.bind(wasm)));
    setStrokeDigest(readDigest(wasm, wasm._canvas_poc02_stroke_digest.bind(wasm)));
    setPreviewDigest(readDigest(wasm, wasm._canvas_poc02_preview_digest.bind(wasm)));
    setNumericDigest(readDigest(wasm, wasm._canvas_poc02_numeric_digest.bind(wasm)));
    setSamples(wasm._canvas_poc02_confirmed_count());
    setEvents(wasm._canvas_poc02_event_count());
  }

  function recordVisible(strokeId: number, sampleTimestamp: number) {
    const visibleTimestamp = performance.now();
    trace.current.push({
      strokeId,
      sampleTimestamp,
      visibleTimestamp,
      latencyMs: Math.max(0, visibleTimestamp - sampleTimestamp),
      frameCount: 1,
    });
    window.__canvasPoc02Trace = trace.current;
    const values = trace.current.map((value) => value.latencyMs);
    setLatency(`p50 ${percentile(values, 0.5).toFixed(2)} ms · p95 ${percentile(values, 0.95).toFixed(2)} ms · p99 ${percentile(values, 0.99).toFixed(2)} ms`);
  }

  function renderPreview(wasm: InkModule, strokeId: number, timestamp: number) {
    check(wasm._canvas_poc02_render(), "render preview");
    requestAnimationFrame(() => recordVisible(strokeId, timestamp));
  }

  function pointerSamples(
    event: React.PointerEvent<HTMLCanvasElement>,
  ): PointerSampleInput[] {
    const native = event.nativeEvent;
    const coalesced = native.getCoalescedEvents?.() ?? [];
    return (coalesced.length > 0 ? coalesced : [native]).map((sample) => ({
      clientX: sample.clientX,
      clientY: sample.clientY,
      pressure: sample.pressure,
      timeStamp: sample.timeStamp,
    }));
  }

  function beginStroke(wasm: InkModule, stroke: QueuedStroke) {
    withSamples(wasm, stroke.canvas, stroke.firstSamples, stroke.timestampBase,
      (packed, timestamps, count) =>
      check(wasm._canvas_poc02_begin(stroke.strokeId,
        stroke.brush === "dab" ? 2 : 1, stroke.brush === "dab" ? 16 : 8,
        packed, timestamps, count, 1), "begin stroke"));
    activeStrokeId.current = stroke.strokeId;
    strokeTimestampBase.current = stroke.timestampBase;
    lastSampleTimestamp.current = stroke.firstSamples.at(-1)?.timeStamp ??
      performance.now();
  }

  function pushStrokeBatch(wasm: InkModule, canvas: HTMLCanvasElement,
    batch: QueuedPointerBatch) {
    lastSampleTimestamp.current = batch.samples.at(-1)?.timeStamp ??
      performance.now();
    withSamples(wasm, canvas, batch.samples, strokeTimestampBase.current,
      (packed, timestamps, count) =>
      check(wasm._canvas_poc02_push_batch(packed, timestamps, count,
        batch.final ? 1 : 0, 1),
      batch.final ? "push final batch" : "push historical batch"));
  }

  function scheduleVisibleAcknowledgement(wasm: InkModule, strokeId: number,
    sampleTimestamp: number) {
    requestAnimationFrame(() => {
      check(wasm._canvas_poc02_visible(), "canonical visible acknowledgement");
      pendingVisible.current = false;
      committedStrokeCount.current += 1;
      window.__canvasPoc02CommittedStrokeCount = committedStrokeCount.current;
      recordVisible(strokeId, sampleTimestamp);
      refreshMetrics(wasm);
      flushQueuedStroke(wasm);
    });
  }

  function commitStroke(wasm: InkModule) {
    check(wasm._canvas_poc02_end(), "commit AddStroke");
    check(wasm._canvas_poc02_render(), "render canonical");
    const committedStrokeId = activeStrokeId.current;
    const committedSampleTimestamp = lastSampleTimestamp.current;
    activePointerId.current = null;
    pendingVisible.current = true;
    scheduleVisibleAcknowledgement(
      wasm, committedStrokeId, committedSampleTimestamp);
    setMessage("Canonical AddStroke committed; Preview retires only after visible ack.");
  }

  function flushQueuedStroke(wasm: InkModule) {
    const queued = queuedStrokes.current.shift();
    if (!queued) return;
    beginStroke(wasm, queued);
    let final = false;
    for (const batch of queued.batches) {
      pushStrokeBatch(wasm, queued.canvas, batch);
      final = batch.final;
    }
    if (final) {
      commitStroke(wasm);
    } else {
      renderPreview(wasm, queued.strokeId, lastSampleTimestamp.current);
      setMessage(`Active ${queued.brush} stroke ${queued.strokeId}`);
    }
  }

  function onPointerDown(event: React.PointerEvent<HTMLCanvasElement>) {
    if (!module || activePointerId.current !== null) return;
    const batch = pointerSamples(event);
    event.currentTarget.setPointerCapture(event.pointerId);
    activePointerId.current = event.pointerId;
    const stroke: QueuedStroke = {
      pointerId: event.pointerId,
      canvas: event.currentTarget,
      strokeId: nextStrokeId.current++,
      brush,
      timestampBase: batch[0]?.timeStamp ?? event.timeStamp,
      firstSamples: batch,
      batches: [],
    };
    if (pendingVisible.current || queuedStrokes.current.length > 0) {
      if (queuedStrokes.current.length >= MAX_QUEUED_STROKES) {
        setMessage("Handoff queue is full; the new stroke was rejected explicitly.");
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
          event.currentTarget.releasePointerCapture(event.pointerId);
        }
        activePointerId.current = null;
        return;
      }
      queuedStrokes.current.push(stroke);
      setMessage(`Queued ${brush} stroke ${stroke.strokeId} behind Canonical handoff.`);
      return;
    }
    beginStroke(module, stroke);
    renderPreview(module, stroke.strokeId, lastSampleTimestamp.current);
    setMessage(`Active ${brush} stroke ${stroke.strokeId}`);
  }

  function onPointerMove(event: React.PointerEvent<HTMLCanvasElement>) {
    if (!module || event.pointerId !== activePointerId.current) return;
    const batch = pointerSamples(event);
    const queued = queuedStrokes.current.at(-1);
    if (queued?.pointerId === event.pointerId) {
      queued.batches.push({samples: batch, final: false});
      return;
    }
    pushStrokeBatch(module, event.currentTarget, {samples: batch, final: false});
    renderPreview(module, activeStrokeId.current, lastSampleTimestamp.current);
  }

  function onPointerUp(event: React.PointerEvent<HTMLCanvasElement>) {
    if (!module || event.pointerId !== activePointerId.current) return;
    const batch = pointerSamples(event);
    const queued = queuedStrokes.current.at(-1);
    if (queued?.pointerId === event.pointerId) {
      queued.batches.push({samples: batch, final: true});
      activePointerId.current = null;
      return;
    }
    pushStrokeBatch(module, event.currentTarget, {samples: batch, final: true});
    commitStroke(module);
  }

  function onPointerCancel(event: React.PointerEvent<HTMLCanvasElement>) {
    if (!module || event.pointerId !== activePointerId.current) return;
    const queued = queuedStrokes.current.at(-1);
    if (queued?.pointerId === event.pointerId) {
      queuedStrokes.current.pop();
      activePointerId.current = null;
      setMessage("Queued stroke cancelled before entering the C++ Runtime.");
      return;
    }
    check(module._canvas_poc02_cancel(), "cancel stroke");
    check(module._canvas_poc02_render(), "clear preview");
    activePointerId.current = null;
    setMessage("Stroke cancelled atomically; Document was not modified.");
  }

  function onLostPointerCapture(event: React.PointerEvent<HTMLCanvasElement>) {
    if (!module || event.pointerId !== activePointerId.current) return;
    const queued = queuedStrokes.current.at(-1);
    if (queued?.pointerId === event.pointerId) {
      queuedStrokes.current.pop();
      activePointerId.current = null;
      setMessage("Queued stroke discarded after pointer capture was lost.");
      return;
    }
    check(module._canvas_poc02_cancel(), "cancel stroke after lost pointer capture");
    check(module._canvas_poc02_render(), "clear preview after lost pointer capture");
    activePointerId.current = null;
    setMessage("Pointer capture was lost; the active stroke was cancelled atomically.");
  }

  async function replayFixture(name: string) {
    if (!module) return;
    let stage = "fetch fixture";
    try {
      const fixture = await fetch(`/fixtures/${name}`).then((response) => response.text());
      stage = "replay fixture";
      const samplePointer = module._malloc(4);
      try {
        withUtf8(module, fixture, (pointer, size) =>
          check(module._canvas_poc02_replay(pointer, size, samplePointer), "replay"));
        setSamples(module.HEAPU32[samplePointer / 4]);
      } finally {
        module._free(samplePointer);
      }
      stage = "render replay";
      check(module._canvas_poc02_render(), "render replay");
      stage = "read document digest";
      setDocumentDigest(readDigest(module,
        module._canvas_poc02_document_digest.bind(module)));
      stage = "read stroke digest";
      setStrokeDigest(readDigest(module,
        module._canvas_poc02_stroke_digest.bind(module)));
      stage = "read preview digest";
      setPreviewDigest(readDigest(module,
        module._canvas_poc02_preview_digest.bind(module)));
      stage = "read numeric digest";
      setNumericDigest(readDigest(module,
        module._canvas_poc02_numeric_digest.bind(module)));
      setEvents(module._canvas_poc02_event_count());
      stage = "fetch golden";
      const goldenName = name.startsWith("dab")
        ? "dab-reference.rgba" : "vector-reference.rgba";
      const goldenBuffer: ArrayBuffer = await fetch(`/goldens/${goldenName}`)
        .then((response) => response.arrayBuffer());
      const expected = new Uint8Array(goldenBuffer);
      stage = "compare golden";
      withBytes(module, expected, (expectedPointer) => {
        const ratioPointer = module._malloc(8);
        const deltaPointer = module._malloc(4);
        try {
          const compareStatus = module._canvas_poc02_compare_golden(expectedPointer,
            expected.byteLength, ratioPointer, deltaPointer);
          if (compareStatus !== 0) {
            const detail = readDigest(module,
              module._canvas_poc02_last_error.bind(module));
            throw new Error(`golden compare failed with ${compareStatus}: ${detail}`);
          }
          const ratio = new Float64Array(module.HEAPU8.buffer, ratioPointer, 1)[0];
          const maxDelta = module.HEAPU32[deltaPointer / 4];
          setGolden(`${(ratio * 100).toFixed(4)}% within ±2 · max Δ ${maxDelta}`);
          if (ratio < 0.999 || maxDelta > 2) {
            throw new Error(
              `visual gate failed: ratio=${ratio}, maxDelta=${maxDelta}`);
          }
        } finally {
          module._free(deltaPointer);
          module._free(ratioPointer);
        }
      });
      setMessage(`${name} replayed through PointerSampleBatch → AddStroke.`);
    } catch (error) {
      throw new Error(`${stage}: ${describeUnknown(error, module)}`);
    }
  }

  async function replayFixtureSafely(name: string) {
    try {
      await replayFixture(name);
    } catch (error) {
      const detail = describeUnknown(error, module);
      setGolden(`Failed: ${detail}`);
      setMessage(`Replay failed: ${detail}`);
    }
  }

  return <main>
    <header>
      <div><p className="eyebrow">Visual Document Runtime · Experimental</p>
        <h1>POC-02 Ink Playground</h1></div>
      <p className="status" data-testid="status">{message}</p>
    </header>
    <section className="toolbar" aria-label="Ink controls">
      <div className="segmented">
        <button className={brush === "vector" ? "selected" : ""}
          onClick={() => setBrush("vector")}>Vector Brush</button>
        <button className={brush === "dab" ? "selected" : ""}
          onClick={() => setBrush("dab")}>Dab Brush</button>
      </div>
      <button onClick={() => void replayFixtureSafely("vector-pressure.ndjson")}>Replay Vector</button>
      <button onClick={() => void replayFixtureSafely("dab-turn.ndjson")}>Replay Dab</button>
    </section>
    <section className="workspace">
      <canvas id="ink-canvas" width="800" height="600"
        onPointerDown={onPointerDown} onPointerMove={onPointerMove}
        onPointerUp={onPointerUp} onPointerCancel={onPointerCancel}
        onLostPointerCapture={onLostPointerCapture}
        aria-label="Canvas POC-02 ink surface" />
      <aside>
        <h2>Live evidence</h2>
        <dl>
          <div><dt>Confirmed samples</dt><dd data-testid="samples">{samples}</dd></div>
          <div><dt>Preview events</dt><dd data-testid="events">{events}</dd></div>
          <div><dt>Latency trace</dt><dd data-testid="latency">{latency}</dd></div>
          <div><dt>Golden metrics</dt><dd data-testid="golden">{golden}</dd></div>
          <div><dt>Stroke digest</dt><dd data-testid="stroke-digest">{strokeDigest}</dd></div>
          <div><dt>Document digest</dt><dd data-testid="document-digest">{documentDigest}</dd></div>
          <div><dt>Preview digest</dt><dd data-testid="preview-digest">{previewDigest}</dd></div>
          <div><dt>Numeric digest</dt><dd data-testid="numeric-digest">{numericDigest}</dd></div>
        </dl>
        <p className="note">Browser timing is an application-level trace. Physical pen and display latency belongs in the Human Ink Gate report.</p>
      </aside>
    </section>
  </main>;
}

createRoot(document.getElementById("root")!).render(<App />);

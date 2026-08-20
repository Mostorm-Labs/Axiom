import { useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import { check, createModule, PointerInput, Poc03Module, readJson,
  withSamples } from "./wasm";
import "./styles.css";

type Tool = "pan" | "vector" | "dab" | "select";
type Scale = 1000 | 10000 | 50000 | 100000;

function coalesced(event: React.PointerEvent<HTMLCanvasElement>): PointerInput[] {
  const native = event.nativeEvent;
  const values = native.getCoalescedEvents?.() ?? [];
  return (values.length ? values : [native]).map((sample) => ({
    clientX: sample.clientX,
    clientY: sample.clientY,
    pressure: sample.pressure,
    timeStamp: sample.timeStamp,
  }));
}

function local(canvas: HTMLCanvasElement, clientX: number, clientY: number) {
  const bounds = canvas.getBoundingClientRect();
  return {
    x: (clientX - bounds.left) * canvas.width / bounds.width,
    y: (clientY - bounds.top) * canvas.height / bounds.height,
  };
}

function App() {
  const [module, setModule] = useState<Poc03Module | null>(null);
  const [tool, setTool] = useState<Tool>("pan");
  const [scale, setScale] = useState<Scale>(1000);
  const [message, setMessage] = useState("Loading C++20 Integrated Ink…");
  const activePointer = useRef<number | null>(null);
  const timestampBase = useRef(0);
  const previous = useRef({x: 0, y: 0});

  useEffect(() => {
    void (async () => {
      try {
        const wasm = await createModule();
        const prepared = readJson(wasm,
          wasm._canvas_poc03_web_prepare_scale(scale)) as {error?: string};
        if (prepared.error) throw new Error(prepared.error);
        window.__canvasPoc03Module = wasm;
        setModule(wasm);
        setMessage("Ready: pan, zoom, write, select, and drag share one RuntimeScene.");
      } catch (error) {
        setMessage(error instanceof Error ? error.message : String(error));
      }
    })();
  }, []);

  function loadScale(next: Scale) {
    if (!module) return;
    setMessage(`Building ${next.toLocaleString()} nodes + ${(next / 5).toLocaleString()} Strokes…`);
    requestAnimationFrame(() => {
      try {
        const prepared = readJson(module,
          module._canvas_poc03_web_prepare_scale(next)) as {error?: string};
        if (prepared.error) throw new Error(prepared.error);
        setScale(next);
        setMessage(`${next.toLocaleString()} nodes ready.`);
      } catch (error) {
        setMessage(error instanceof Error ? error.message : String(error));
      }
    });
  }

  function onPointerDown(event: React.PointerEvent<HTMLCanvasElement>) {
    if (!module || activePointer.current !== null) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    activePointer.current = event.pointerId;
    const point = local(event.currentTarget, event.clientX, event.clientY);
    previous.current = point;
    if (tool === "vector" || tool === "dab") {
      const samples = coalesced(event);
      timestampBase.current = samples[0]?.timeStamp ?? event.timeStamp;
      withSamples(module, event.currentTarget, samples, timestampBase.current,
        (packed, timestamps, count) => check(module._canvas_poc03_web_ink_begin(
          tool === "dab" ? 2 : 1, event.pointerId, packed, timestamps,
          count, 1), "Ink begin"));
    } else if (tool === "select") {
      check(module._canvas_poc03_web_select_begin(point.x, point.y),
        "select begin");
    }
  }

  function onPointerMove(event: React.PointerEvent<HTMLCanvasElement>) {
    if (!module || activePointer.current !== event.pointerId) return;
    const point = local(event.currentTarget, event.clientX, event.clientY);
    if (tool === "vector" || tool === "dab") {
      withSamples(module, event.currentTarget, coalesced(event),
        timestampBase.current, (packed, timestamps, count) => check(
          module._canvas_poc03_web_ink_push(event.pointerId, packed, timestamps,
            count, 0, 1), "Ink push"));
    } else if (tool === "select") {
      check(module._canvas_poc03_web_select_move(point.x, point.y),
        "select drag");
    } else {
      check(module._canvas_poc03_web_transform(previous.current.x,
        previous.current.y, point.x, point.y, 1, 1), "pan");
    }
    previous.current = point;
  }

  function finishPointer(event: React.PointerEvent<HTMLCanvasElement>) {
    if (!module || activePointer.current !== event.pointerId) return;
    const point = local(event.currentTarget, event.clientX, event.clientY);
    if (tool === "vector" || tool === "dab") {
      withSamples(module, event.currentTarget, coalesced(event),
        timestampBase.current, (packed, timestamps, count) => check(
          module._canvas_poc03_web_ink_push(event.pointerId, packed, timestamps,
            count, 1, 1), "Ink commit"));
      requestAnimationFrame(() => {
        if (module._canvas_poc03_web_ink_visible() === 0) {
          setMessage("Canonical Stroke visible; Preview retired after acknowledgement.");
        }
      });
    } else if (tool === "select") {
      check(module._canvas_poc03_web_select_move(point.x, point.y),
        "final select drag");
      check(module._canvas_poc03_web_select_end(), "select end");
    }
    activePointer.current = null;
  }

  function onCancel(event: React.PointerEvent<HTMLCanvasElement>) {
    if (!module || activePointer.current !== event.pointerId) return;
    if (tool === "vector" || tool === "dab") {
      module._canvas_poc03_web_ink_cancel();
    } else if (tool === "select") {
      module._canvas_poc03_web_select_end();
    }
    activePointer.current = null;
  }

  function onWheel(event: React.WheelEvent<HTMLCanvasElement>) {
    if (!module) return;
    event.preventDefault();
    const point = local(event.currentTarget, event.clientX, event.clientY);
    const zoom = Math.exp(-event.deltaY * 0.0015);
    check(module._canvas_poc03_web_transform(point.x, point.y, point.x,
      point.y, zoom, 1), "zoom");
  }

  return <main>
    <header>
      <div><p>Visual Document Runtime · Experimental</p>
        <h1>POC-03 Integrated Ink</h1></div>
      <output data-testid="status">{message}</output>
    </header>
    <nav aria-label="Integrated playground controls">
      {([1000, 10000, 50000, 100000] as Scale[]).map((value) =>
        <button className={scale === value ? "selected" : ""}
          onClick={() => loadScale(value)} key={value}>{value / 1000}K</button>)}
      <span />
      {(["pan", "vector", "dab", "select"] as Tool[]).map((value) =>
        <button className={tool === value ? "selected" : ""}
          onClick={() => setTool(value)} key={value}>{value}</button>)}
    </nav>
    <canvas id="scene" width="1280" height="720"
      aria-label="POC-03 Integrated Ink surface"
      onPointerDown={onPointerDown} onPointerMove={onPointerMove}
      onPointerUp={finishPointer} onPointerCancel={onCancel}
      onWheel={onWheel} />
    <footer>{scale.toLocaleString()} base nodes · {(scale / 5).toLocaleString()} canonical Strokes · Pointer coalescing → C++ InputRouter</footer>
  </main>;
}

createRoot(document.getElementById("root")!).render(<App />);

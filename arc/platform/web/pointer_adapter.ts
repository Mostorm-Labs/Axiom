/**
 * Arc Web input edge. This is intentionally a browser adapter, not a React
 * component: PointerEvents are packed once and handed to the host WASM bridge.
 */

export type ArcWebSample = {
  pointerId: number;
  sequence: number;
  timestampUs: number;
  x: number;
  y: number;
  pressure: number;
  tiltX: number;
  tiltY: number;
  phase: "down" | "move" | "up" | "cancel";
  provenance: "current" | "coalesced";
};

export type ArcWebPointerBatch = {
  schemaVersion: number;
  coordinateSpace: "view-logical";
  viewId: bigint;
  viewportRevision: bigint;
  deviceId: bigint;
  inputCapabilities: bigint;
  tool: "mouse" | "pen" | "touch";
  viewToWorld: readonly [number, number, number, number, number, number];
  samples: readonly ArcWebSample[];
};

export interface ArcWebInputBridge {
  pushPointerBatch(batch: ArcWebPointerBatch): void;
  cancelPointer(pointerId: number): void;
}

export type ArcWebPointerAdapterOptions = {
  canvas: HTMLCanvasElement;
  bridge: ArcWebInputBridge;
  useDesynchronizedPreview?: boolean;
};

function samplesFor(event: PointerEvent, phase: ArcWebSample["phase"]): ArcWebSample[] {
  const coalesced = event.getCoalescedEvents?.() ?? [];
  const source = coalesced.length === 0 ? [event] : coalesced;
  return source.map((sample, index) => ({
    pointerId: sample.pointerId,
    sequence: index,
    timestampUs: Math.round(sample.timeStamp * 1000),
    x: sample.offsetX,
    y: sample.offsetY,
    pressure: sample.pressure,
    tiltX: sample.tiltX,
    tiltY: sample.tiltY,
    phase,
    provenance: coalesced.length === 0 ? "current" : "coalesced",
  }));
}

function toolFor(event: PointerEvent): ArcWebPointerBatch["tool"] {
  if (event.pointerType === "pen") return "pen";
  if (event.pointerType === "touch") return "touch";
  return "mouse";
}

export function attachArcWebPointerAdapter(
  options: ArcWebPointerAdapterOptions,
): () => void {
  const { canvas, bridge } = options;
  canvas.style.touchAction = "none";
  const active = new Set<number>();
  const sequences = new Map<number, number>();
  const deviceIds = new Map<string, bigint>();
  let nextDeviceId = 1n;
  const moveEvent = "onpointerrawupdate" in canvas ? "pointerrawupdate" : "pointermove";

  const deviceIdFor = (event: PointerEvent): bigint => {
    const key = `${event.pointerType}:${event.isPrimary ? "primary" : "secondary"}`;
    let deviceId = deviceIds.get(key);
    if (deviceId === undefined) {
      deviceId = nextDeviceId++;
      deviceIds.set(key, deviceId);
    }
    return deviceId;
  };

  const push = (event: PointerEvent, phase: ArcWebSample["phase"]): void => {
    const samples = samplesFor(event, phase).map((sample) => {
      const sequence = (sequences.get(sample.pointerId) ?? 0) + 1;
      sequences.set(sample.pointerId, sequence);
      return {
        ...sample,
        sequence,
      };
    });
    bridge.pushPointerBatch({
      schemaVersion: 0,
      coordinateSpace: "view-logical",
      viewId: 1n,
      viewportRevision: 1n,
      deviceId: deviceIdFor(event),
      inputCapabilities:
        (event.pointerType === "pen" ? (1n | 2n | 4n) : 0n) |
        ("getCoalescedEvents" in event ? 16n : 0n),
      tool: toolFor(event),
      viewToWorld: [1, 0, 0, 1, 0, 0],
      samples,
    });
  };

  const down = (event: PointerEvent) => {
    if (!active.has(event.pointerId)) {
      active.add(event.pointerId);
      canvas.setPointerCapture(event.pointerId);
    }
    push(event, "down");
  };
  const move = (event: PointerEvent) => {
    if (active.has(event.pointerId)) push(event, "move");
  };
  const up = (event: PointerEvent) => {
    if (active.delete(event.pointerId)) {
      push(event, "up");
      sequences.delete(event.pointerId);
    }
  };
  const cancel = (event: PointerEvent) => {
    if (active.delete(event.pointerId)) {
      push(event, "cancel");
      bridge.cancelPointer(event.pointerId);
      sequences.delete(event.pointerId);
    }
  };

  canvas.addEventListener("pointerdown", down);
  canvas.addEventListener(moveEvent, move as EventListener);
  canvas.addEventListener("pointerup", up);
  canvas.addEventListener("pointercancel", cancel);
  return () => {
    canvas.removeEventListener("pointerdown", down);
    canvas.removeEventListener(moveEvent, move as EventListener);
    canvas.removeEventListener("pointerup", up);
    canvas.removeEventListener("pointercancel", cancel);
    active.clear();
    sequences.clear();
    deviceIds.clear();
  };
}

/**
 * Web Preview Presentation Target host adapter.
 *
 * The Arc C++ bridge receives only opaque target/generation values. This file
 * owns the DOM canvas, WebGL2 context, resize and context-loss lifecycle. It
 * is deliberately independent from React state and does not present the
 * Canonical canvas.
 */

export type ArcWebPresentationReceipt = {
  targetGeneration: bigint;
  presentationId: bigint;
  evidence: "render-complete" | "gpu-submitted" | "present-accepted";
  submitTimestampUs: number;
  presentTimestampUs: number;
};

export interface ArcWebPresentationBridge {
  attachTarget(target: {
    targetId: bigint;
    targetGeneration: bigint;
    widthPixels: number;
    heightPixels: number;
    devicePixelRatio: number;
    // The C ABI carries a uint64_t only. JavaScript objects must be kept in
    // this adapter's registry and never cross the WASM boundary directly.
    opaquePlatformHandle: bigint;
  }): void;
  surfaceLost(targetGeneration: bigint): void;
  detachTarget(targetGeneration: bigint): void;
  resizeTarget(target: {
    targetGeneration: bigint;
    widthPixels: number;
    heightPixels: number;
    devicePixelRatio: number;
  }): void;
}

export type ArcWebPresentationTargetOptions = {
  canvas: HTMLCanvasElement;
  bridge: ArcWebPresentationBridge;
  targetId?: bigint;
  desynchronized?: boolean;
};

export type ArcWebPresentationTarget = {
  context: WebGL2RenderingContext;
  generation: () => bigint;
  receipt: (presentationId: bigint, submitTimestampUs: number) => ArcWebPresentationReceipt;
  dispose: () => void;
};

function resize(
  canvas: HTMLCanvasElement,
): { widthPixels: number; heightPixels: number; devicePixelRatio: number } {
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.round(canvas.clientWidth * dpr));
  const height = Math.max(1, Math.round(canvas.clientHeight * dpr));
  if (canvas.width !== width) canvas.width = width;
  if (canvas.height !== height) canvas.height = height;
  return { widthPixels: width, heightPixels: height, devicePixelRatio: dpr };
}

// WASM/C ABI has no object references. Keep a process-local, non-recycled
// handle table for WebGL contexts so a context loss cannot accidentally make
// an old target refer to a replacement context.
const contextHandles = new WeakMap<WebGL2RenderingContext, bigint>();
const contexts = new Map<bigint, WebGL2RenderingContext>();
let nextContextHandle = 1n;

function handleFor(context: WebGL2RenderingContext): bigint {
  const existing = contextHandles.get(context);
  if (existing !== undefined) {
    contexts.set(existing, context);
    return existing;
  }
  const handle = nextContextHandle++;
  contextHandles.set(context, handle);
  contexts.set(handle, context);
  return handle;
}

export function resolveArcWebPresentationHandle(
  handle: bigint,
): WebGL2RenderingContext | undefined {
  return contexts.get(handle);
}

export function attachArcWebPresentationTarget(
  options: ArcWebPresentationTargetOptions,
): ArcWebPresentationTarget {
  const { canvas, bridge } = options;
  const context = canvas.getContext("webgl2", {
    alpha: true,
    antialias: false,
    desynchronized: options.desynchronized ?? false,
    premultipliedAlpha: true,
  });
  if (!context) throw new Error("Arc requires a WebGL2 preview target");

  let currentGeneration = 1n;
  const targetId = options.targetId ?? 1n;
  const opaquePlatformHandle = handleFor(context);
  const initialSize = resize(canvas);
  bridge.attachTarget({
    targetId,
    targetGeneration: currentGeneration,
    ...initialSize,
    opaquePlatformHandle,
  });
  const observer = new ResizeObserver(() => {
    const size = resize(canvas);
    bridge.resizeTarget({ targetGeneration: currentGeneration, ...size });
  });
  observer.observe(canvas);

  const contextLost = (event: Event): void => {
    event.preventDefault();
    bridge.surfaceLost(currentGeneration);
  };
  const contextRestored = (): void => {
    currentGeneration += 1n;
    const size = resize(canvas);
    bridge.attachTarget({
      targetId,
      targetGeneration: currentGeneration,
      ...size,
      opaquePlatformHandle,
    });
  };
  canvas.addEventListener("webglcontextlost", contextLost);
  canvas.addEventListener("webglcontextrestored", contextRestored);

  return {
    context,
    generation: () => currentGeneration,
    receipt: (presentationId, submitTimestampUs) => ({
      targetGeneration: currentGeneration,
      presentationId,
      evidence: "present-accepted",
      submitTimestampUs,
      presentTimestampUs: Math.round(performance.now() * 1000),
    }),
    dispose: () => {
      observer.disconnect();
      canvas.removeEventListener("webglcontextlost", contextLost);
      canvas.removeEventListener("webglcontextrestored", contextRestored);
      bridge.detachTarget(currentGeneration);
      contexts.delete(opaquePlatformHandle);
    },
  };
}

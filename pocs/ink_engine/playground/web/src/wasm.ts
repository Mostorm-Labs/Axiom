export interface InkModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAPF32: Float32Array;
  _malloc(size: number): number;
  _free(pointer: number): void;
  _canvas_poc02_reset(): number;
  _canvas_poc02_replay(ndjson: number, size: number, sampleCount: number): number;
  _canvas_poc02_surface_create(selector: number): number;
  _canvas_poc02_render(): number;
  _canvas_poc02_readback(output: number, capacity: number): number;
  _canvas_poc02_compare_golden(expected: number, size: number,
    ratio: number, maxDelta: number): number;
  _canvas_poc02_last_error(output: number, capacity: number): number;
  _canvas_poc02_begin(strokeId: number, brushType: number, brushSize: number,
    packed: number, timestamps: number, count: number, dpr: number): number;
  _canvas_poc02_push_batch(packed: number, timestamps: number, count: number,
    final: number, dpr: number): number;
  _canvas_poc02_end(): number;
  _canvas_poc02_cancel(): number;
  _canvas_poc02_visible(): number;
  _canvas_poc02_document_digest(output: number, capacity: number): number;
  _canvas_poc02_stroke_digest(output: number, capacity: number): number;
  _canvas_poc02_preview_digest(output: number, capacity: number): number;
  _canvas_poc02_numeric_digest(output: number, capacity: number): number;
  _canvas_poc02_event_count(): number;
  _canvas_poc02_confirmed_count(): number;
  UTF8ToString(pointer: number): string;
  getExceptionMessage(error: unknown): [string, string];
}

type ModuleFactory = (options?: Record<string, unknown>) => Promise<InkModule>;

declare global {
  interface Window {
    __canvasPoc02Factory?: ModuleFactory;
    __canvasPoc02FactoryReady?: Promise<ModuleFactory>;
    __canvasPoc02Module?: InkModule;
    __canvasPoc02Trace?: LatencySample[];
  }
}

export interface LatencySample {
  strokeId: number;
  sampleTimestamp: number;
  visibleTimestamp: number;
  latencyMs: number;
  frameCount: number;
}

export interface PointerSampleInput {
  clientX: number;
  clientY: number;
  pressure: number;
  timeStamp: number;
}

export async function createModule(): Promise<InkModule> {
  const factory = window.__canvasPoc02Factory ??
    await window.__canvasPoc02FactoryReady;
  if (!factory) throw new Error("Canvas POC-02 WASM factory did not load");
  return factory({ locateFile: (name: string) => `/wasm/${name}` });
}

export function check(status: number, action: string): void {
  if (status !== 0) throw new Error(`${action} failed with ${status}`);
}

export function withUtf8<T>(module: InkModule, value: string,
  use: (pointer: number, size: number) => T): T {
  const encoded = new TextEncoder().encode(value);
  const pointer = module._malloc(encoded.byteLength + 1);
  try {
    module.HEAPU8.set(encoded, pointer);
    module.HEAPU8[pointer + encoded.byteLength] = 0;
    return use(pointer, encoded.byteLength);
  } finally {
    module._free(pointer);
  }
}

export function withBytes<T>(module: InkModule, value: Uint8Array,
  use: (pointer: number) => T): T {
  const pointer = module._malloc(value.byteLength || 1);
  try {
    module.HEAPU8.set(value, pointer);
    return use(pointer);
  } finally {
    module._free(pointer);
  }
}

export function withSamples<T>(module: InkModule, canvas: HTMLCanvasElement,
  samples: readonly PointerSampleInput[], baseTimestamp: number,
  use: (packed: number, timestamps: number, count: number) => T): T {
  const packedPointer = module._malloc(samples.length * 3 * 4);
  const timestampsPointer = module._malloc(samples.length * 4);
  try {
    const packed = module.HEAPF32.subarray(
      packedPointer / 4, packedPointer / 4 + samples.length * 3);
    const timestamps = module.HEAPU32.subarray(
      timestampsPointer / 4, timestampsPointer / 4 + samples.length);
    const bounds = canvas.getBoundingClientRect();
    samples.forEach((sample, index) => {
      packed[index * 3] = (sample.clientX - bounds.left) * canvas.width / bounds.width;
      packed[index * 3 + 1] = (sample.clientY - bounds.top) * canvas.height / bounds.height;
      packed[index * 3 + 2] = sample.pressure > 0 ? sample.pressure : 0.5;
      timestamps[index] = Math.max(0, Math.round((sample.timeStamp - baseTimestamp) * 1000));
    });
    return use(packedPointer, timestampsPointer, samples.length);
  } finally {
    module._free(timestampsPointer);
    module._free(packedPointer);
  }
}

export function readDigest(module: InkModule,
  getter: (output: number, capacity: number) => number): string {
  const required = getter(0, 0);
  const pointer = module._malloc(required);
  try {
    getter(pointer, required);
    return module.UTF8ToString(pointer);
  } finally {
    module._free(pointer);
  }
}

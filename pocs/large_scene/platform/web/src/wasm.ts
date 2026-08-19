export interface Poc03Module {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAPF32: Float32Array;
  _malloc(size: number): number;
  _free(pointer: number): void;
  _canvas_poc03_web_prepare_scale(nodes: number): number;
  _canvas_poc03_web_reset(): void;
  _canvas_poc03_web_ink_begin(brush: number, pointerId: number,
    packed: number, timestamps: number, count: number, dpr: number): number;
  _canvas_poc03_web_ink_push(pointerId: number, packed: number,
    timestamps: number, count: number, final: number, dpr: number): number;
  _canvas_poc03_web_ink_visible(): number;
  _canvas_poc03_web_ink_cancel(): number;
  _canvas_poc03_web_transform(previousX: number, previousY: number,
    currentX: number, currentY: number, scale: number, dpr: number): number;
  _canvas_poc03_web_select_begin(x: number, y: number): number;
  _canvas_poc03_web_select_move(x: number, y: number): number;
  _canvas_poc03_web_select_end(): number;
  UTF8ToString(pointer: number): string;
}

type ModuleFactory = (options?: Record<string, unknown>) => Promise<Poc03Module>;

declare global {
  interface Window {
    __canvasPoc03Factory?: ModuleFactory;
    __canvasPoc03FactoryReady?: Promise<ModuleFactory>;
    __canvasPoc03Module?: Poc03Module;
  }
}

export interface PointerInput {
  clientX: number;
  clientY: number;
  pressure: number;
  timeStamp: number;
}

export async function createModule(): Promise<Poc03Module> {
  const factory = window.__canvasPoc03Factory ??
    await window.__canvasPoc03FactoryReady;
  if (!factory) throw new Error("Canvas POC-03 WASM factory did not load");
  return factory({ locateFile: (name: string) => `/wasm/${name}` });
}

export function check(status: number, action: string): void {
  if (status !== 0) throw new Error(`${action} failed with ${status}`);
}

export function readJson(module: Poc03Module, pointer: number): unknown {
  return JSON.parse(module.UTF8ToString(pointer));
}

export function withSamples<T>(module: Poc03Module,
  canvas: HTMLCanvasElement, samples: readonly PointerInput[],
  timestampBase: number,
  use: (packed: number, timestamps: number, count: number) => T): T {
  const packedPointer = module._malloc(samples.length * 3 * 4);
  const timestampsPointer = module._malloc(samples.length * 4);
  try {
    const packed = module.HEAPF32.subarray(packedPointer / 4,
      packedPointer / 4 + samples.length * 3);
    const timestamps = module.HEAPU32.subarray(timestampsPointer / 4,
      timestampsPointer / 4 + samples.length);
    const bounds = canvas.getBoundingClientRect();
    samples.forEach((sample, index) => {
      packed[index * 3] = (sample.clientX - bounds.left) *
        canvas.width / bounds.width;
      packed[index * 3 + 1] = (sample.clientY - bounds.top) *
        canvas.height / bounds.height;
      packed[index * 3 + 2] = sample.pressure > 0 ? sample.pressure : 0.5;
      timestamps[index] = Math.max(0,
        Math.round((sample.timeStamp - timestampBase) * 1000));
    });
    return use(packedPointer, timestampsPointer, samples.length);
  } finally {
    module._free(timestampsPointer);
    module._free(packedPointer);
  }
}

export interface CanvasModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAPF64: Float64Array;
  _malloc(size: number): number;
  _free(pointer: number): void;
  _canvas_poc_web_reset(): number;
  _canvas_poc_web_load_assets(checker: number, checkerSize: number, font: number, fontSize: number): number;
  _canvas_poc_web_replay(ndjson: number, size: number): number;
  _canvas_poc_web_digest(buffer: number, size: number, required: number): number;
  _canvas_poc_web_core_conformance(buffer: number, size: number, required: number): number;
  _canvas_poc_web_surface_create(selector: number): number;
  _canvas_poc_web_render(): number;
  _canvas_poc_web_readback(buffer: number, size: number, required: number): number;
  _canvas_poc_web_compare_golden(expected: number, size: number, ratio: number, maxDelta: number): number;
  _canvas_poc_web_backend(): number;
  UTF8ToString(pointer: number): string;
}

declare global {
  interface Window {
    __canvasPocFactory?: ModuleFactory;
    __canvasPocFactoryReady?: Promise<ModuleFactory>;
    __canvasPocModule?: CanvasModule;
  }
}

type ModuleFactory = (options?: Record<string, unknown>) => Promise<CanvasModule>;

export async function createModule(): Promise<CanvasModule> {
  const factory = window.__canvasPocFactory ?? await window.__canvasPocFactoryReady;
  if (!factory) throw new Error("Canvas POC WASM factory was not loaded");
  return factory({
    locateFile: (name: string) => `/wasm/${name}`,
  });
}

export function withBytes<T>(module: CanvasModule, bytes: Uint8Array, use: (pointer: number) => T): T {
  const pointer = module._malloc(bytes.byteLength || 1);
  try {
    module.HEAPU8.set(bytes, pointer);
    return use(pointer);
  } finally {
    module._free(pointer);
  }
}

export function withUtf8<T>(module: CanvasModule, value: string, use: (pointer: number, size: number) => T): T {
  const encoded = new TextEncoder().encode(value);
  const terminated = new Uint8Array(encoded.byteLength + 1);
  terminated.set(encoded);
  return withBytes(module, terminated, (pointer) => use(pointer, encoded.byteLength));
}

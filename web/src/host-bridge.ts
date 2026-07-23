export const CANVAS_PROTOCOL_VERSION = 1 as const;

export type HostMessageType =
  | "set-content"
  | "set-video-source"
  | "play"
  | "pause"
  | "seek";

export type ClientMessageType =
  | "ready"
  | "content-changed"
  | "playing"
  | "paused"
  | "time-update"
  | "error";

type JsonRecord = Record<string, unknown>;

export interface HostMessage {
  protocolVersion: typeof CANVAS_PROTOCOL_VERSION;
  type: HostMessageType;
  nodeId: string;
  payload: JsonRecord;
}

const hostTypes = new Set<HostMessageType>([
  "set-content",
  "set-video-source",
  "play",
  "pause",
  "seek"
]);

const clientTypes = new Set<ClientMessageType>([
  "ready",
  "content-changed",
  "playing",
  "paused",
  "time-update",
  "error"
]);

function isRecord(value: unknown): value is JsonRecord {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function hasOnlyKeys(value: JsonRecord, keys: readonly string[]): boolean {
  const actual = Object.keys(value);
  return actual.length === keys.length && actual.every((key) => keys.includes(key));
}

function isNodeId(value: unknown): value is string {
  return typeof value === "string" && value.length > 0 && value.length <= 256;
}

function isFiniteNonNegative(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value) && value >= 0;
}

function isEditorState(value: unknown): boolean {
  return (typeof value === "string" && value.length > 0) || isRecord(value);
}

function validHostPayload(type: HostMessageType, payload: JsonRecord): boolean {
  switch (type) {
    case "set-content":
      return hasOnlyKeys(payload, ["editorState"]) &&
        isEditorState(payload.editorState);
    case "set-video-source":
      return hasOnlyKeys(payload, ["source"]) &&
        typeof payload.source === "string" && payload.source.length > 0;
    case "play":
    case "pause":
      return hasOnlyKeys(payload, []);
    case "seek":
      return hasOnlyKeys(payload, ["timeSeconds"]) &&
        isFiniteNonNegative(payload.timeSeconds);
  }
}

function validClientPayload(type: ClientMessageType, payload: JsonRecord): boolean {
  switch (type) {
    case "ready":
      return hasOnlyKeys(payload, []);
    case "content-changed":
      return hasOnlyKeys(payload, ["editorState"]) &&
        isRecord(payload.editorState);
    case "playing":
    case "paused":
      return hasOnlyKeys(payload, ["currentTime"]) &&
        isFiniteNonNegative(payload.currentTime);
    case "time-update":
      return hasOnlyKeys(payload, ["currentTime", "duration"]) &&
        isFiniteNonNegative(payload.currentTime) &&
        (payload.duration === null || isFiniteNonNegative(payload.duration));
    case "error":
      return hasOnlyKeys(payload, ["operation", "message"]) &&
        typeof payload.operation === "string" && payload.operation.length > 0 &&
        typeof payload.message === "string" && payload.message.length > 0;
  }
}

export function decodeHostMessage(value: unknown): HostMessage {
  let parsed: unknown;
  try {
    parsed = typeof value === "string" ? JSON.parse(value) : undefined;
  } catch {
    throw new Error("Invalid Canvas host message");
  }

  if (!isRecord(parsed) ||
      !hasOnlyKeys(parsed, ["protocolVersion", "type", "nodeId", "payload"]) ||
      parsed.protocolVersion !== CANVAS_PROTOCOL_VERSION ||
      typeof parsed.type !== "string" ||
      !hostTypes.has(parsed.type as HostMessageType) ||
      !isNodeId(parsed.nodeId) ||
      !isRecord(parsed.payload) ||
      !validHostPayload(parsed.type as HostMessageType, parsed.payload)) {
    throw new Error("Invalid Canvas host message");
  }

  return parsed as unknown as HostMessage;
}

export function encodeClientMessage(
  nodeId: string,
  type: ClientMessageType,
  payload: JsonRecord
): string {
  if (!isNodeId(nodeId) || !clientTypes.has(type) || !isRecord(payload) ||
      !validClientPayload(type, payload)) {
    throw new Error("Invalid Canvas client message");
  }
  return JSON.stringify({
    protocolVersion: CANVAS_PROTOCOL_VERSION,
    type,
    nodeId,
    payload
  });
}

export function postToHost(
  nodeId: string,
  type: ClientMessageType,
  payload: JsonRecord
): void {
  const bridge = (window as Window & {
    chrome?: { webview?: { postMessage(value: string): void } };
  }).chrome?.webview;
  bridge?.postMessage(encodeClientMessage(nodeId, type, payload));
}

export function nodeIdFromLocation(defaultNodeId: string): string {
  const candidate = new URLSearchParams(window.location.search).get("nodeId");
  return isNodeId(candidate) ? candidate : defaultNodeId;
}

import {
  type CanvasWebviewBridge,
  type CanvasWebviewMessageEvent,
  decodeHostMessage,
  getWebviewBridge,
  nodeIdFromLocation,
  postToHost,
  type HostMessage
} from "./host-bridge";
import { normalizeVideoSource } from "./video-source";

export interface CanvasVideoElementLike {
  currentTime: number;
  duration: number;
  error: { code: number } | null;
  src: string;
  addEventListener(type: string, listener: () => void): void;
  load(): void;
  play(): Promise<void>;
  pause(): void;
}

function describeError(error: unknown): string {
  return error instanceof Error && error.message.length > 0
    ? error.message
    : "Unknown media error";
}

function errorReporter(
  nodeId: string,
  bridge: Pick<CanvasWebviewBridge, "postMessage">
): (operation: string, error: unknown) => void {
  return (operation, error) => {
    postToHost(nodeId, "error", {
      operation,
      message: describeError(error)
    }, bridge);
  };
}

export function installVideoEvents(
  player: CanvasVideoElementLike,
  nodeId: string,
  bridge: Pick<CanvasWebviewBridge, "postMessage">
): void {
  const reportError = errorReporter(nodeId, bridge);
  const currentTime = () =>
    Number.isFinite(player.currentTime) && player.currentTime >= 0
      ? player.currentTime
      : 0;

  player.addEventListener("playing", () => {
    postToHost(nodeId, "playing", { currentTime: currentTime() }, bridge);
  });
  player.addEventListener("pause", () => {
    postToHost(nodeId, "paused", { currentTime: currentTime() }, bridge);
  });
  player.addEventListener("timeupdate", () => {
    postToHost(nodeId, "time-update", {
      currentTime: currentTime(),
      duration:
        Number.isFinite(player.duration) && player.duration >= 0
          ? player.duration
          : null
    }, bridge);
  });
  player.addEventListener("error", () => {
    const mediaError = player.error;
    reportError(
      "media",
      new Error(mediaError ? `MediaError ${mediaError.code}` : "Media failed")
    );
  });
}

export function installVideoHostBridge(
  player: CanvasVideoElementLike,
  nodeId: string,
  bridge: CanvasWebviewBridge
): () => void {
  const reportError = errorReporter(nodeId, bridge);
  const handleMessage = async (message: HostMessage): Promise<void> => {
    if (message.nodeId !== nodeId) return;
    switch (message.type) {
      case "set-video-source": {
        const source = normalizeVideoSource(message.payload.source);
        if (source === null) throw new Error("Video source is not allowed");
        player.src = source;
        player.load();
        return;
      }
      case "play":
        await player.play();
        return;
      case "pause":
        player.pause();
        return;
      case "seek":
        player.currentTime = message.payload.timeSeconds as number;
        return;
      case "set-content":
        return;
    }
  };

  const listener = (event: CanvasWebviewMessageEvent) => {
    let message: HostMessage;
    try {
      message = decodeHostMessage(event.data);
    } catch (error) {
      reportError("decode-message", error);
      return;
    }
    void handleMessage(message).catch((error: unknown) => {
      reportError(message.type, error);
    });
  };

  bridge.addEventListener("message", listener);
  postToHost(nodeId, "ready", {}, bridge);
  return () => bridge.removeEventListener?.("message", listener);
}

export function startVideoPlayer(
  player: HTMLVideoElement,
  bridge: CanvasWebviewBridge | undefined = getWebviewBridge()
): void {
  const nodeId = nodeIdFromLocation("video");
  if (!bridge) return;
  const playerAdapter = player as unknown as CanvasVideoElementLike;
  installVideoEvents(playerAdapter, nodeId, bridge);
  installVideoHostBridge(playerAdapter, nodeId, bridge);
}

if (typeof document !== "undefined") {
  const player = document.getElementById("player");
  if (!(player instanceof HTMLVideoElement)) {
    throw new Error("Canvas video element is missing");
  }
  startVideoPlayer(player);
}

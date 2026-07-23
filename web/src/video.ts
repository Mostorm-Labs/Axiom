import {
  decodeHostMessage,
  nodeIdFromLocation,
  postToHost,
  type HostMessage
} from "./host-bridge";
import { normalizeVideoSource } from "./video-source";

function describeError(error: unknown): string {
  return error instanceof Error && error.message.length > 0
    ? error.message
    : "Unknown media error";
}

function startVideoPlayer(player: HTMLVideoElement): void {
  const nodeId = nodeIdFromLocation("video");
  const reportError = (operation: string, error: unknown) => {
    postToHost(nodeId, "error", {
      operation,
      message: describeError(error)
    });
  };
  const currentTime = () =>
    Number.isFinite(player.currentTime) && player.currentTime >= 0
      ? player.currentTime
      : 0;

  player.addEventListener("playing", () => {
    postToHost(nodeId, "playing", { currentTime: currentTime() });
  });
  player.addEventListener("pause", () => {
    postToHost(nodeId, "paused", { currentTime: currentTime() });
  });
  player.addEventListener("timeupdate", () => {
    postToHost(nodeId, "time-update", {
      currentTime: currentTime(),
      duration:
        Number.isFinite(player.duration) && player.duration >= 0
          ? player.duration
          : null
    });
  });
  player.addEventListener("error", () => {
    const mediaError = player.error;
    reportError(
      "media",
      new Error(mediaError ? `MediaError ${mediaError.code}` : "Media failed")
    );
  });

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

  const webview = (window as Window & {
    chrome?: {
      webview?: {
        addEventListener(
          type: "message",
          listener: (event: MessageEvent<unknown>) => void
        ): void;
      };
    };
  }).chrome?.webview;
  webview?.addEventListener("message", (event) => {
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
  });

  postToHost(nodeId, "ready", {});
}

if (typeof document !== "undefined") {
  const player = document.getElementById("player");
  if (!(player instanceof HTMLVideoElement)) {
    throw new Error("Canvas video element is missing");
  }
  startVideoPlayer(player);
}

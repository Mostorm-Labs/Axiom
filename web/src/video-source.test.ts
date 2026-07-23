import { describe, expect, it } from "vitest";
import { normalizeVideoSource } from "./video-source";
import { installVideoHostBridge, installVideoEvents } from "./video";

class MockBridge {
  readonly messages: string[] = [];
  private listener: ((event: { data: unknown }) => void) | undefined;

  postMessage(value: string): void {
    this.messages.push(value);
  }

  addEventListener(
    _type: "message",
    listener: (event: { data: unknown }) => void
  ): void {
    this.listener = listener;
  }

  emit(data: unknown): void {
    this.listener?.({ data });
  }
}

class MockVideo {
  currentTime = 0;
  duration = 120;
  src = "";
  error: { code: number } | null = null;
  readonly events = new Map<string, () => void>();
  playResult: Promise<void> = Promise.resolve();

  addEventListener(type: string, listener: () => void): void {
    this.events.set(type, listener);
  }

  emit(type: string): void {
    this.events.get(type)?.();
  }

  load(): void {}

  play(): Promise<void> {
    return this.playResult;
  }

  pause(): void {}
}

describe("video source policy", () => {
  it("accepts the exact local media host and explicit remote HTTPS", () => {
    expect(
      normalizeVideoSource("https://media.canvas.local/demo%20clip.mp4")
    ).toBe("https://media.canvas.local/demo%20clip.mp4");
    expect(normalizeVideoSource("https://cdn.example/video.mp4")).toBe(
      "https://cdn.example/video.mp4"
    );
  });

  it.each([
    "http://media.canvas.local/demo.mp4",
    "https://media.canvas.local.attacker.example/demo.mp4",
    "https://media.canvas.local@attacker.example/demo.mp4",
    "https://canvas.local/video.mp4",
    "https://media.canvas.local:444/video.mp4",
    "data:video/mp4;base64,AAAA",
    "file:///C:/video.mp4",
    "javascript:alert(1)",
    "not a URL"
  ])("rejects %s", (source) => {
    expect(normalizeVideoSource(source)).toBeNull();
  });
});

describe("video host bridge and media events", () => {
  it("encodes playing, paused, and time-update events", () => {
    const bridge = new MockBridge();
    const player = new MockVideo();
    installVideoEvents(player, "video-1", bridge);

    player.currentTime = 12.5;
    player.emit("playing");
    player.emit("pause");
    player.emit("timeupdate");

    expect(bridge.messages.map((message) => JSON.parse(message))).toEqual([
      expect.objectContaining({
        type: "playing",
        payload: { currentTime: 12.5 }
      }),
      expect.objectContaining({
        type: "paused",
        payload: { currentTime: 12.5 }
      }),
      expect.objectContaining({
        type: "time-update",
        payload: { currentTime: 12.5, duration: 120 }
      })
    ]);
  });

  it("reports play rejection and media errors through the bridge", async () => {
    const bridge = new MockBridge();
    const player = new MockVideo();
    installVideoEvents(player, "video-1", bridge);
    installVideoHostBridge(player, "video-1", bridge);
    expect(JSON.parse(bridge.messages[0] ?? "")).toMatchObject({
      type: "ready",
      nodeId: "video-1",
      payload: {}
    });
    bridge.emit("{malformed-json");
    expect(JSON.parse(bridge.messages.at(-1) ?? "")).toMatchObject({
      type: "error",
      nodeId: "video-1",
      payload: {
        operation: "decode-message",
        message: "Invalid Canvas host message"
      }
    });
    player.playResult = Promise.reject(new Error("autoplay blocked"));
    bridge.emit(JSON.stringify({
      protocolVersion: 1,
      type: "play",
      nodeId: "video-1",
      payload: {}
    }));
    await Promise.resolve();
    await Promise.resolve();
    expect(JSON.parse(bridge.messages.at(-1) ?? "")).toMatchObject({
      type: "error",
      payload: { operation: "play", message: "autoplay blocked" }
    });

    player.error = { code: 3 };
    player.emit("error");
    expect(JSON.parse(bridge.messages.at(-1) ?? "")).toMatchObject({
      type: "error",
      payload: { operation: "media", message: "MediaError 3" }
    });
  });
});

import { describe, expect, it } from "vitest";
import { normalizeVideoSource } from "./video-source";

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

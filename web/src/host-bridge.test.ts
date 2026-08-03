import { describe, expect, it } from "vitest";
import { decodeHostMessage, encodeClientMessage } from "./host-bridge";

describe("host bridge", () => {
  it("accepts versioned messages with type-specific payloads", () => {
    expect(
      decodeHostMessage(
        JSON.stringify({
          protocolVersion: 1,
          type: "set-content",
          nodeId: "text-1",
          payload: { editorState: "{}" }
        })
      ).type
    ).toBe("set-content");
    expect(
      decodeHostMessage(
        JSON.stringify({
          protocolVersion: 1,
          type: "seek",
          nodeId: "video-1",
          payload: { timeSeconds: 12.5 }
        })
      ).type
    ).toBe("seek");
  });

  it.each([
    ["malformed JSON", "{"],
    ["JSON null", "null"],
    ["JSON array", "[]"],
    ["JSON primitive", "7"],
    [
      "wrong protocol",
      JSON.stringify({
        protocolVersion: 2,
        type: "pause",
        nodeId: "video-1",
        payload: {}
      })
    ],
    [
      "unknown type",
      JSON.stringify({
        protocolVersion: 1,
        type: "execute-script",
        nodeId: "video-1",
        payload: {}
      })
    ],
    [
      "empty node id",
      JSON.stringify({
        protocolVersion: 1,
        type: "pause",
        nodeId: "",
        payload: {}
      })
    ],
    [
      "null payload",
      JSON.stringify({
        protocolVersion: 1,
        type: "pause",
        nodeId: "video-1",
        payload: null
      })
    ],
    [
      "array payload",
      JSON.stringify({
        protocolVersion: 1,
        type: "pause",
        nodeId: "video-1",
        payload: []
      })
    ],
    [
      "missing video source",
      JSON.stringify({
        protocolVersion: 1,
        type: "set-video-source",
        nodeId: "video-1",
        payload: {}
      })
    ],
    [
      "negative seek",
      JSON.stringify({
        protocolVersion: 1,
        type: "seek",
        nodeId: "video-1",
        payload: { timeSeconds: -1 }
      })
    ]
  ])("rejects %s", (_description, value) => {
    expect(() => decodeHostMessage(value)).toThrow("Invalid Canvas host message");
  });

  it("emits a protocol-v1 JSON object containing structured editor state", () => {
    const encoded = encodeClientMessage("text-1", "content-changed", {
      editorState: { root: { children: [] } }
    });
    const parsed = JSON.parse(encoded) as Record<string, unknown>;

    expect(parsed).toEqual({
      protocolVersion: 1,
      type: "content-changed",
      nodeId: "text-1",
      payload: { editorState: { root: { children: [] } } }
    });
  });

  it("rejects invalid client-message fields at runtime", () => {
    expect(() =>
      encodeClientMessage("", "ready", {})
    ).toThrow("Invalid Canvas client message");
    expect(() =>
      encodeClientMessage(
        "text-1",
        "unknown" as "ready",
        {}
      )
    ).toThrow("Invalid Canvas client message");
    expect(() =>
      encodeClientMessage("text-1", "ready", [] as unknown as Record<string, unknown>)
    ).toThrow("Invalid Canvas client message");
  });
});

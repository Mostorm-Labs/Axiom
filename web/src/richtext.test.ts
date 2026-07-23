import { describe, expect, it } from "vitest";
import {
  HOST_STATE_UPDATE_TAG,
  shouldPostEditorUpdate
} from "./richtext";

describe("rich-text update origin", () => {
  it("suppresses host-applied editor state to avoid an echo loop", () => {
    expect(shouldPostEditorUpdate(new Set([HOST_STATE_UPDATE_TAG]))).toBe(false);
  });

  it("posts ordinary editor updates", () => {
    expect(shouldPostEditorUpdate(new Set(["history-merge"]))).toBe(true);
  });
});

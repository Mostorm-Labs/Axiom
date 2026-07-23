import { describe, expect, it } from "vitest";
import {
  HOST_STATE_UPDATE_TAG,
  installRichTextHostBridge,
  shouldPostEditorUpdate
} from "./richtext";

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

class MockEditor {
  parsed: unknown[] = [];
  applied: Array<{ state: unknown; tag: string | undefined }> = [];
  parseError: Error | undefined;
  private updateListener: ((update: {
    editorState: { toJSON(): unknown };
    tags: ReadonlySet<string>;
  }) => void) | undefined;

  parseEditorState(value: unknown): unknown {
    if (this.parseError) throw this.parseError;
    this.parsed.push(value);
    return { toJSON: () => value };
  }

  setEditorState(state: unknown, options?: { tag?: string }): void {
    this.applied.push({ state, tag: options?.tag });
    const serializable = state as { toJSON(): unknown };
    this.updateListener?.({
      editorState: serializable,
      tags: new Set(options?.tag ? [options.tag] : [])
    });
  }

  registerUpdateListener(listener: (update: {
    editorState: { toJSON(): unknown };
    tags: ReadonlySet<string>;
  }) => void): () => void {
    this.updateListener = listener;
    return () => {
      this.updateListener = undefined;
    };
  }

  emitUpdate(editorState: unknown, tags: ReadonlySet<string>): void {
    this.updateListener?.({
      editorState: { toJSON: () => editorState },
      tags
    });
  }
}

describe("rich-text update origin", () => {
  it("suppresses host-applied editor state to avoid an echo loop", () => {
    expect(shouldPostEditorUpdate(new Set([HOST_STATE_UPDATE_TAG]))).toBe(false);
  });

  it("posts ordinary editor updates", () => {
    expect(shouldPostEditorUpdate(new Set(["history-merge"]))).toBe(true);
  });
});

describe("rich-text host bridge", () => {
  it("posts ready, applies host content with a non-echo tag, and reports parse errors", () => {
    const bridge = new MockBridge();
    const editor = new MockEditor();
    installRichTextHostBridge(editor, "note-1", bridge);

    expect(JSON.parse(bridge.messages[0] ?? "")).toMatchObject({
      type: "ready",
      nodeId: "note-1",
      payload: {}
    });

    bridge.emit(JSON.stringify({
      protocolVersion: 1,
      type: "set-content",
      nodeId: "note-1",
      payload: { editorState: { root: { children: [] } } }
    }));
    expect(editor.parsed).toHaveLength(1);
    expect(editor.applied[0]?.tag).toBe(HOST_STATE_UPDATE_TAG);
    expect(bridge.messages).toHaveLength(1);

    editor.parseError = new Error("bad editor state");
    bridge.emit(JSON.stringify({
      protocolVersion: 1,
      type: "set-content",
      nodeId: "note-1",
      payload: { editorState: { root: { children: [] } } }
    }));
    expect(JSON.parse(bridge.messages.at(-1) ?? "")).toMatchObject({
      type: "error",
      nodeId: "note-1",
      payload: { operation: "set-content", message: "bad editor state" }
    });
  });

  it("does not echo host-tagged updates but encodes ordinary updates", () => {
    const bridge = new MockBridge();
    const editor = new MockEditor();
    installRichTextHostBridge(editor, "note-1", bridge);
    bridge.messages.length = 0;
    editor.emitUpdate(
      { root: { children: [] } },
      new Set([HOST_STATE_UPDATE_TAG])
    );
    expect(bridge.messages).toHaveLength(0);

    editor.emitUpdate(
      { root: { children: [] } },
      new Set(["history-merge"])
    );
    expect(JSON.parse(bridge.messages[0] ?? "")).toMatchObject({
      type: "content-changed",
      nodeId: "note-1"
    });
  });
});

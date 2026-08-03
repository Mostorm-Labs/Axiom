import { createEmptyHistoryState, registerHistory } from "@lexical/history";
import { HeadingNode, QuoteNode, registerRichText } from "@lexical/rich-text";
import { createEditor, type SerializedEditorState } from "lexical";
import {
  type CanvasWebviewBridge,
  type CanvasWebviewMessageEvent,
  decodeHostMessage,
  getWebviewBridge,
  nodeIdFromLocation,
  postToHost
} from "./host-bridge";

export const HOST_STATE_UPDATE_TAG = "canvas-host-state";

export interface RichTextEditorLike {
  parseEditorState(value: string | SerializedEditorState): unknown;
  setEditorState(state: unknown, options?: { tag?: string }): void;
  registerUpdateListener(
    listener: (update: {
      editorState: RichTextUpdateState;
      tags: ReadonlySet<string>;
    }) => void
  ): () => void;
}

export interface RichTextUpdateState {
  toJSON(): unknown;
}

export function shouldPostEditorUpdate(tags: ReadonlySet<string>): boolean {
  return !tags.has(HOST_STATE_UPDATE_TAG);
}

export function postRichTextUpdate(
  nodeId: string,
  editorState: unknown,
  tags: ReadonlySet<string>,
  bridge?: Pick<CanvasWebviewBridge, "postMessage">
): void {
  if (!shouldPostEditorUpdate(tags)) return;
  postToHost(nodeId, "content-changed", { editorState }, bridge);
}

function describeError(error: unknown): string {
  return error instanceof Error && error.message.length > 0
    ? error.message
    : "Unknown editor error";
}

export function installRichTextHostBridge(
  editor: RichTextEditorLike,
  nodeId: string,
  bridge: CanvasWebviewBridge
): () => void {
  const reportError = (operation: string, error: unknown) => {
    postToHost(nodeId, "error", {
      operation,
      message: describeError(error)
    }, bridge);
  };

  const listener = (event: CanvasWebviewMessageEvent) => {
    try {
      const message = decodeHostMessage(event.data);
      if (message.type !== "set-content" || message.nodeId !== nodeId) return;
      const serialized = message.payload.editorState as
        | string
        | SerializedEditorState;
      const nextState = editor.parseEditorState(serialized);
      editor.setEditorState(nextState, { tag: HOST_STATE_UPDATE_TAG });
    } catch (error) {
      reportError("set-content", error);
    }
  };

  const unregisterUpdate = editor.registerUpdateListener(
    ({ editorState, tags }) => {
      try {
        postRichTextUpdate(nodeId, editorState.toJSON(), tags, bridge);
      } catch (error) {
        reportError("editor-update", error);
      }
    }
  );
  bridge.addEventListener("message", listener);
  postToHost(nodeId, "ready", {}, bridge);
  return () => {
    unregisterUpdate();
    bridge.removeEventListener?.("message", listener);
  };
}

export function startRichTextEditor(
  root: HTMLElement,
  bridge: CanvasWebviewBridge | undefined = getWebviewBridge()
): void {
  const nodeId = nodeIdFromLocation("rich-text");
  const reportError = (operation: string, error: unknown) => {
    postToHost(nodeId, "error", {
      operation,
      message: describeError(error)
    }, bridge);
  };

  const editor = createEditor({
    namespace: "MostormCanvasRichText",
    nodes: [HeadingNode, QuoteNode],
    onError(error) {
      reportError("editor-update", error);
    }
  });
  registerRichText(editor);
  registerHistory(editor, createEmptyHistoryState(), 300);
  editor.setRootElement(root);

  if (bridge) {
    installRichTextHostBridge(
      editor as unknown as RichTextEditorLike,
      nodeId,
      bridge
    );
  }
}

if (typeof document !== "undefined") {
  const root = document.getElementById("editor");
  if (!(root instanceof HTMLElement)) {
    throw new Error("Canvas rich-text root is missing");
  }
  startRichTextEditor(root);
}

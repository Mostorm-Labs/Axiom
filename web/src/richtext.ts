import { createEmptyHistoryState, registerHistory } from "@lexical/history";
import { HeadingNode, QuoteNode, registerRichText } from "@lexical/rich-text";
import { createEditor, type SerializedEditorState } from "lexical";
import {
  decodeHostMessage,
  nodeIdFromLocation,
  postToHost
} from "./host-bridge";

export const HOST_STATE_UPDATE_TAG = "canvas-host-state";

export function shouldPostEditorUpdate(tags: ReadonlySet<string>): boolean {
  return !tags.has(HOST_STATE_UPDATE_TAG);
}

function describeError(error: unknown): string {
  return error instanceof Error && error.message.length > 0
    ? error.message
    : "Unknown editor error";
}

function startRichTextEditor(root: HTMLElement): void {
  const nodeId = nodeIdFromLocation("rich-text");
  const reportError = (operation: string, error: unknown) => {
    postToHost(nodeId, "error", {
      operation,
      message: describeError(error)
    });
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

  editor.registerUpdateListener(({ editorState, tags }) => {
    if (!shouldPostEditorUpdate(tags)) return;
    postToHost(nodeId, "content-changed", {
      editorState: editorState.toJSON()
    });
  });

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
  });

  postToHost(nodeId, "ready", {});
}

if (typeof document !== "undefined") {
  const root = document.getElementById("editor");
  if (!(root instanceof HTMLElement)) {
    throw new Error("Canvas rich-text root is missing");
  }
  startRichTextEditor(root);
}

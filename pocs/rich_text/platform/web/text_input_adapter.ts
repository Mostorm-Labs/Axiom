export interface CanvasPoc04Module {
  _malloc(size: number): number;
  _free(pointer: number): void;
  stringToUTF8(value: string, pointer: number, maxBytes: number): void;
  lengthBytesUTF8(value: string): number;
  UTF8ToString(pointer: number): string;
  _canvas_poc04_web_create(): number;
  _canvas_poc04_web_destroy(session: number): void;
  _canvas_poc04_web_focus(session: number): number;
  _canvas_poc04_web_blur(session: number): number;
  _canvas_poc04_web_begin_composition(session: number): number;
  _canvas_poc04_web_update_composition(session: number, pointer: number, size: number): number;
  _canvas_poc04_web_update_composition_with_selection(
    session: number, pointer: number, size: number, selectionStart: number, selectionEnd: number,
  ): number;
  _canvas_poc04_web_commit_composition(session: number): number;
  _canvas_poc04_web_cancel_composition(session: number): number;
  _canvas_poc04_web_insert(session: number, pointer: number, size: number): number;
  _canvas_poc04_web_delete_surrounding(session: number, before: number, after: number): number;
  _canvas_poc04_web_undo(session: number): number;
  _canvas_poc04_web_redo(session: number): number;
  _canvas_poc04_web_digest(session: number): number;
  _canvas_poc04_web_presented_text(session: number): number;
  _canvas_poc04_web_selection_location(session: number): number;
  _canvas_poc04_web_selection_length(session: number): number;
  _canvas_poc04_web_caret_x(session: number, layoutWidth: number): number;
  _canvas_poc04_web_caret_y(session: number, layoutWidth: number): number;
  _canvas_poc04_web_caret_width(session: number, layoutWidth: number): number;
  _canvas_poc04_web_caret_height(session: number, layoutWidth: number): number;
}

// The textarea is only an IME transport. TextDocument remains authoritative in WASM.
export class WebTextInputAdapter {
  private composing = false;

  constructor(
    private readonly module: CanvasPoc04Module,
    private readonly session: number,
    private readonly textarea: HTMLTextAreaElement,
  ) {
    textarea.autocapitalize = "off";
    textarea.autocomplete = "off";
    textarea.spellcheck = false;
    textarea.addEventListener("compositionstart", this.onCompositionStart);
    textarea.addEventListener("compositionupdate", this.onCompositionUpdate);
    textarea.addEventListener("compositionend", this.onCompositionEnd);
    textarea.addEventListener("beforeinput", this.onBeforeInput);
    textarea.addEventListener("focus", () => this.check(module._canvas_poc04_web_focus(session)));
    textarea.addEventListener("blur", () => {
      this.composing = false;
      this.check(module._canvas_poc04_web_blur(session));
    });
  }

  destroy(): void {
    if (this.composing) this.module._canvas_poc04_web_cancel_composition(this.session);
    this.textarea.removeEventListener("compositionstart", this.onCompositionStart);
    this.textarea.removeEventListener("compositionupdate", this.onCompositionUpdate);
    this.textarea.removeEventListener("compositionend", this.onCompositionEnd);
    this.textarea.removeEventListener("beforeinput", this.onBeforeInput);
  }

  private readonly onCompositionStart = (): void => {
    this.composing = true;
    this.check(this.module._canvas_poc04_web_begin_composition(this.session));
  };

  private readonly onCompositionUpdate = (event: CompositionEvent): void => {
    const value = event.data ?? "";
    const selection = this.compositionSelection(value);
    this.withUtf8(value, (pointer, size) =>
      this.module._canvas_poc04_web_update_composition_with_selection(
        this.session, pointer, size, selection.start, selection.end));
  };

  private readonly onCompositionEnd = (event: CompositionEvent): void => {
    if (!this.composing) return;
    this.withUtf8(event.data ?? "", (pointer, size) =>
      this.module._canvas_poc04_web_update_composition(this.session, pointer, size));
    this.check(this.module._canvas_poc04_web_commit_composition(this.session));
    this.composing = false;
    this.textarea.value = "";
  };

  private readonly onBeforeInput = (event: InputEvent): void => {
    if (event.isComposing || this.composing) return;
    if (event.inputType === "insertText" || event.inputType === "insertLineBreak" ||
        event.inputType === "insertParagraph" || event.inputType === "insertFromPaste") {
      event.preventDefault();
      const value = event.inputType === "insertLineBreak" ||
        event.inputType === "insertParagraph" ? "\n" : (event.data ?? "");
      this.withUtf8(value, (pointer, size) =>
        this.module._canvas_poc04_web_insert(this.session, pointer, size));
      this.textarea.value = "";
    } else if (event.inputType === "deleteContentBackward") {
      event.preventDefault();
      this.check(this.module._canvas_poc04_web_delete_surrounding(this.session, 1, 0));
    } else if (event.inputType === "deleteContentForward") {
      event.preventDefault();
      this.check(this.module._canvas_poc04_web_delete_surrounding(this.session, 0, 1));
    } else if (event.inputType === "deleteByCut") {
      event.preventDefault();
      this.check(this.module._canvas_poc04_web_delete_surrounding(this.session, 0, 0));
    } else if (event.inputType === "historyUndo") {
      event.preventDefault();
      this.check(this.module._canvas_poc04_web_undo(this.session));
    } else if (event.inputType === "historyRedo") {
      event.preventDefault();
      this.check(this.module._canvas_poc04_web_redo(this.session));
    }
  };

  private withUtf8(value: string, callback: (pointer: number, size: number) => number): void {
    const size = this.module.lengthBytesUTF8(value);
    const pointer = this.module._malloc(size + 1);
    try {
      this.module.stringToUTF8(value, pointer, size + 1);
      this.check(callback(pointer, size));
    } finally {
      this.module._free(pointer);
    }
  }

  private compositionSelection(value: string): { start: number; end: number } {
    const start = Math.max(0, Math.min(value.length, this.textarea.selectionStart ?? value.length));
    const end = Math.max(start, Math.min(value.length, this.textarea.selectionEnd ?? value.length));
    return { start, end };
  }

  private check(status: number): void {
    if (status !== 0) throw new Error(`Canvas POC-04 status ${status}`);
  }
}

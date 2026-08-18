import { expect, test } from "@playwright/test";
import { WebTextInputAdapter } from "../text_input_adapter";

test("browser composition and beforeinput events preserve atomic boundaries", async ({ page }) => {
  await page.setContent('<textarea id="ime"></textarea>');
  const records = await page.evaluate((AdapterSource) => {
    // Playwright serializes the function body but not imported module state.
    // Recreate the audited adapter class from its source for this browser-only
    // DOM event conformance test.
    const WebTextInputAdapter = (0, eval)(`(${AdapterSource})`);
    const calls: Array<[string, string?]> = [];
    const memory = new Uint8Array(4096);
    let next = 32;
    const module = {
      _malloc(size: number) { const value = next; next += size; return value; },
      _free() {},
      lengthBytesUTF8(value: string) { return new TextEncoder().encode(value).length; },
      stringToUTF8(value: string, pointer: number) {
        const encoded = new TextEncoder().encode(value); memory.set(encoded, pointer); memory[pointer + encoded.length] = 0;
      },
      UTF8ToString(pointer: number) { let end = pointer; while (memory[end]) end += 1; return new TextDecoder().decode(memory.slice(pointer, end)); },
      _canvas_poc04_web_focus() { calls.push(["focus"]); return 0; },
      _canvas_poc04_web_blur() { calls.push(["blur"]); return 0; },
      _canvas_poc04_web_begin_composition() { calls.push(["begin"]); return 0; },
      _canvas_poc04_web_update_composition(_session: number, pointer: number) { calls.push(["update", module.UTF8ToString(pointer)]); return 0; },
      _canvas_poc04_web_update_composition_with_selection(_session: number, pointer: number, _size: number, start: number, end: number) { calls.push(["update-selection", `${module.UTF8ToString(pointer)}:${start}:${end}`]); return 0; },
      _canvas_poc04_web_commit_composition() { calls.push(["commit"]); return 0; },
      _canvas_poc04_web_cancel_composition() { calls.push(["cancel"]); return 0; },
      _canvas_poc04_web_insert(_session: number, pointer: number) { calls.push(["insert", module.UTF8ToString(pointer)]); return 0; },
      _canvas_poc04_web_delete_surrounding(_session: number, before: number, after: number) { calls.push(["delete", `${before}:${after}`]); return 0; },
      _canvas_poc04_web_undo() { calls.push(["undo"]); return 0; },
      _canvas_poc04_web_redo() { calls.push(["redo"]); return 0; },
    };
    const textarea = document.querySelector("textarea")!;
    const adapter = new WebTextInputAdapter(module, 1, textarea);
    textarea.focus();
    textarea.dispatchEvent(new CompositionEvent("compositionstart"));
    textarea.dispatchEvent(new CompositionEvent("compositionupdate", { data: "zhong" }));
    textarea.dispatchEvent(new CompositionEvent("compositionend", { data: "中" }));
    textarea.dispatchEvent(new InputEvent("beforeinput", { inputType: "insertText", data: "!", cancelable: true }));
    textarea.dispatchEvent(new InputEvent("beforeinput", { inputType: "deleteContentBackward", cancelable: true }));
    textarea.dispatchEvent(new InputEvent("beforeinput", { inputType: "deleteContentForward", cancelable: true }));
    textarea.dispatchEvent(new InputEvent("beforeinput", { inputType: "historyUndo", cancelable: true }));
    textarea.dispatchEvent(new InputEvent("beforeinput", { inputType: "historyRedo", cancelable: true }));
    adapter.destroy();
    return calls;
  }, WebTextInputAdapter.toString());
  expect(records).toEqual([
    ["focus"], ["begin"], ["update-selection", "zhong:0:0"], ["update", "中"],
    ["commit"], ["insert", "!"], ["delete", "1:0"],
    ["delete", "0:1"], ["undo"], ["redo"],
  ]);
});

import { WebTextInputAdapter, type CanvasPoc04Module } from "./text_input_adapter.js";

interface PhysicalEvent {
  sequence: number;
  event: string;
  data: string;
  inputType?: string;
  isComposing?: boolean;
}

interface PhysicalReport {
  schema_version: 1;
  platform: "web";
  protocol: "Chrome composition/beforeinput";
  controlled_flow: "ni hao -> 你好";
  controlled_flow_passed: boolean;
  final_text: string;
  selection: [number, number];
  caret: [number, number, number, number];
  digest: string;
  browser: string;
  observed_composition_start: boolean;
  observed_composition_update: boolean;
  observed_composition_end: boolean;
  events: PhysicalEvent[];
}

declare global {
  interface Window {
    CanvasPoc04PhysicalReport?: PhysicalReport;
  }
}

async function main(): Promise<void> {
  // The Emscripten ES module is produced by the separate CMake build and is
  // copied beside this recorder for physical validation.
  // @ts-expect-error generated module has no source-tree declaration file
  const moduleFactory = (await import("./canvas_poc04_web.js")).default as
    (options: { locateFile(name: string): string }) => Promise<CanvasPoc04Module>;
  const module = await moduleFactory({ locateFile: (name) => `./${name}` });
  const session = module._canvas_poc04_web_create();
  if (!session) throw new Error("could not create POC-04 Web session");

  const textarea = document.querySelector<HTMLTextAreaElement>("#ime");
  const status = document.querySelector<HTMLElement>("#status");
  const output = document.querySelector<HTMLPreElement>("#output");
  const download = document.querySelector<HTMLButtonElement>("#download");
  if (!textarea || !status || !output || !download) {
    throw new Error("physical recorder markup is incomplete");
  }

  const events: PhysicalEvent[] = [];
  const record = (event: Event): void => {
    const composition = event instanceof CompositionEvent ? event.data ?? "" : "";
    const input = event instanceof InputEvent ? event : undefined;
    events.push({
      sequence: events.length,
      event: event.type,
      data: composition || input?.data || "",
      inputType: input?.inputType,
      isComposing: input?.isComposing,
    });
    window.setTimeout(update, 0);
  };
  for (const name of ["compositionstart", "compositionupdate", "compositionend",
                      "beforeinput", "input"]) {
    textarea.addEventListener(name, record);
  }

  const adapter = new WebTextInputAdapter(module, session, textarea);
  const layoutWidth = 720;
  const update = (): void => {
    const text = module.UTF8ToString(module._canvas_poc04_web_presented_text(session));
    const selection: [number, number] = [
      module._canvas_poc04_web_selection_location(session),
      module._canvas_poc04_web_selection_length(session),
    ];
    const report: PhysicalReport = {
      schema_version: 1,
      platform: "web",
      protocol: "Chrome composition/beforeinput",
      controlled_flow: "ni hao -> 你好",
      controlled_flow_passed: text === "你好",
      final_text: text,
      selection,
      caret: [
        module._canvas_poc04_web_caret_x(session, layoutWidth),
        module._canvas_poc04_web_caret_y(session, layoutWidth),
        module._canvas_poc04_web_caret_width(session, layoutWidth),
        module._canvas_poc04_web_caret_height(session, layoutWidth),
      ],
      digest: module.UTF8ToString(module._canvas_poc04_web_digest(session)),
      browser: navigator.userAgent,
      observed_composition_start: events.some((event) => event.event === "compositionstart"),
      observed_composition_update: events.some((event) => event.event === "compositionupdate"),
      observed_composition_end: events.some((event) => event.event === "compositionend"),
      events,
    };
    window.CanvasPoc04PhysicalReport = report;
    output.textContent = JSON.stringify(report, null, 2);
    status.textContent = report.controlled_flow_passed
      ? "PASS: 你好 committed to the C++ Runtime. Download the JSON report."
      : "Waiting: focus the box, use Microsoft Pinyin, and commit 你好.";
    status.dataset.passed = String(report.controlled_flow_passed);
  };

  download.addEventListener("click", () => {
    update();
    const blob = new Blob([`${JSON.stringify(window.CanvasPoc04PhysicalReport, null, 2)}\n`],
                          { type: "application/json" });
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = "poc04-chrome-ime.json";
    link.click();
    URL.revokeObjectURL(link.href);
  });

  window.addEventListener("beforeunload", () => {
    adapter.destroy();
    module._canvas_poc04_web_destroy(session);
  });
  textarea.focus();
  update();
}

void main().catch((error: unknown) => {
  const status = document.querySelector<HTMLElement>("#status");
  if (status) status.textContent = `Recorder failed: ${String(error)}`;
  throw error;
});

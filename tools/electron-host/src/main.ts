import { app, BrowserWindow, dialog, ipcMain, type IpcMainEvent, type IpcMainInvokeEvent } from "electron";
import { randomBytes, randomUUID } from "node:crypto";
import { spawn, type ChildProcess } from "node:child_process";
import net from "node:net";
import { join } from "node:path";
import { TextDecoder } from "node:util";

const MAX_LINE_BYTES = 1024 * 1024;
const MAX_OUTBOUND_BYTES = 1024 * 1024;
const MAX_OUTBOUND_MESSAGES = 256;
// A shutdown envelope is currently about 110 bytes.  Reserve enough room for
// it so normal backpressure cannot turn a graceful exit into a forced kill.
const EXIT_CONTROL_RESERVE_BYTES = 512;
const MAX_CONNECT_ATTEMPTS = 20;
const RETRY_BASE_DELAY_MS = 50;
const RETRY_MAX_DELAY_MS = 500;

const allowedCommands = new Set([
  "set-tool", "set-mode", "create-embedded", "set-embedded-bounds",
  "delete-node", "enter-interaction", "leave-interaction", "shutdown",
  "open-document", "save-document"
]);

const nativeEvents = new Set([
  "ready", "response", "document-state", "selection-changed",
  "embedded-state", "diagnostics", "fatal-error"
]);

type NativeEvent = {
  protocolVersion: 1;
  type: string;
  requestId: string;
  payload: Record<string, unknown>;
};

let child: ChildProcess | undefined;
let socket: net.Socket | undefined;
let window: BrowserWindow | undefined;
let nativeReady = false;
let nativeFailure: string | undefined;
let quitting = false;
let childExited = false;
let helloSent = false;
let connectAttempts = 0;
let retryTimer: NodeJS.Timeout | undefined;
let quitTimeout: NodeJS.Timeout | undefined;
let writeBlocked = false;
let queuedBytes = 0;
const outbound: string[] = [];

function command(type: string, payload: Record<string, unknown>) {
  return JSON.stringify({
    protocolVersion: 1,
    type,
    requestId: randomUUID(),
    payload
  }) + "\n";
}

function setReady(ready: boolean) {
  nativeReady = ready;
  window?.webContents.send("canvas-ready", ready);
}

function reportNativeFailure(message: string) {
  nativeFailure = message;
  setReady(false);
  window?.webContents.send("canvas-error", message);
  console.error(message);
}

function fatalStartup(message: string) {
  console.error(message);
  dialog.showErrorBox("Canvas host failed to start", message);
  app.exit(1);
}

function isTrustedSender(event: IpcMainEvent | IpcMainInvokeEvent) {
  return window !== undefined &&
    !window.isDestroyed() &&
    event.sender === window.webContents;
}

function stopRetrying() {
  if (retryTimer) clearTimeout(retryTimer);
  retryTimer = undefined;
}

function clearOutbound() {
  outbound.length = 0;
  queuedBytes = 0;
  writeBlocked = false;
}

function socketCanWrite(candidate = socket) {
  return candidate !== undefined &&
    candidate.writable &&
    !candidate.destroyed &&
    candidate === socket;
}

function closeSocket() {
  const current = socket;
  socket = undefined;
  helloSent = false;
  clearOutbound();
  if (current && !current.destroyed) current.destroy();
}

function enqueueOrWrite(line: string, exitControl = false) {
  if (!socketCanWrite()) return false;
  const lineBytes = Buffer.byteLength(line);
  if (lineBytes > MAX_LINE_BYTES + 1) return false;
  if (exitControl) {
    // Once shutdown is requested, no command that has not yet reached the
    // socket may remain behind it. Preserve writeBlocked so the control frame
    // still observes the stream's backpressure and waits for drain.
    outbound.length = 0;
    queuedBytes = 0;
  }
  if (writeBlocked) {
    if (exitControl) {
      if (outbound.length >= MAX_OUTBOUND_MESSAGES ||
          queuedBytes + lineBytes > MAX_OUTBOUND_BYTES) return false;
      outbound.push(line);
      queuedBytes += lineBytes;
      return true;
    }
    if (outbound.length >= MAX_OUTBOUND_MESSAGES - 1 ||
        queuedBytes + lineBytes >
          MAX_OUTBOUND_BYTES - EXIT_CONTROL_RESERVE_BYTES) {
      console.warn("Dropping Canvas command because the outbound queue is full");
      return false;
    }
    outbound.push(line);
    queuedBytes += lineBytes;
    return true;
  }

  writeBlocked = !socket!.write(line);
  return true;
}

function flushOutbound(candidate: net.Socket) {
  if (candidate !== socket || candidate.destroyed) return;
  writeBlocked = false;
  while (outbound.length > 0 && !writeBlocked && socketCanWrite(candidate)) {
    const line = outbound.shift()!;
    queuedBytes -= Buffer.byteLength(line);
    writeBlocked = !candidate.write(line);
  }
}

function send(type: string, payload: Record<string, unknown>, requireReady = true,
              exitControl = false) {
  if (!allowedCommands.has(type) || (requireReady && !nativeReady) ||
      (exitControl && type !== "shutdown") || (quitting && !exitControl)) {
    return false;
  }
  return enqueueOrWrite(command(type, payload), exitControl);
}

function isPlainObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isNativeEvent(value: unknown): value is NativeEvent {
  if (!isPlainObject(value)) return false;
  const keys = Object.keys(value);
  if (keys.length !== 4 || !keys.every((key) =>
    key === "protocolVersion" || key === "type" || key === "requestId" || key === "payload")) {
    return false;
  }
  return value.protocolVersion === 1 &&
    typeof value.type === "string" && nativeEvents.has(value.type) &&
    typeof value.requestId === "string" && value.requestId.length > 0 &&
    isPlainObject(value.payload);
}

function handleNativeEvent(candidate: net.Socket, line: Buffer) {
  // Frame with Buffer before decoding, so UTF-8 can never be split across
  // socket chunks. Fatal decoding rejects malformed IPC text instead of
  // silently replacing bytes before JSON validation.
  let decoded: string;
  try {
    decoded = new TextDecoder("utf-8", { fatal: true }).decode(line);
  } catch {
    candidate.destroy();
    return;
  }
  let message: unknown;
  try {
    message = JSON.parse(decoded);
  } catch {
    candidate.destroy();
    return;
  }
  if (!isNativeEvent(message)) {
    candidate.destroy();
    return;
  }
  if (message.type === "ready" && candidate === socket && !candidate.destroyed) {
    // A TCP/named-pipe connection alone is not a healthy session.  Restore a
    // full, still-bounded reconnect budget only after the authenticated native
    // peer has produced a valid ready envelope on the current socket.
    connectAttempts = 0;
    setReady(true);
  }
}

function consumeSocketData(candidate: net.Socket, chunk: Buffer, state: { pending: Buffer }) {
  let offset = 0;
  while (offset < chunk.length) {
    const newline = chunk.indexOf(0x0a, offset);
    if (newline < 0) {
      const tail = chunk.subarray(offset);
      const combinedLength = state.pending.length + tail.length;
      const lastByte = tail.length > 0
        ? tail[tail.length - 1]
        : state.pending[state.pending.length - 1];
      const validPendingCrLfPrefix =
        combinedLength === MAX_LINE_BYTES + 1 && lastByte === 0x0d;
      if (combinedLength > MAX_LINE_BYTES && !validPendingCrLfPrefix) {
        candidate.destroy();
        return;
      }
      state.pending = state.pending.length === 0
        ? Buffer.from(tail)
        : Buffer.concat([state.pending, tail]);
      return;
    }
    // Match the native server's boundary: LF and a possible CR terminator are
    // excluded from the 1 MiB JSON-line budget.
    const framedLength = state.pending.length + newline - offset;
    const lastByte = newline > offset
      ? chunk[newline - 1]
      : state.pending[state.pending.length - 1];
    const jsonLength = framedLength - (lastByte === 0x0d ? 1 : 0);
    if (jsonLength > MAX_LINE_BYTES) {
      candidate.destroy();
      return;
    }
    let line = state.pending.length === 0
      ? chunk.subarray(offset, newline)
      : Buffer.concat([state.pending, chunk.subarray(offset, newline)]);
    state.pending = Buffer.alloc(0);
    if (line.length > 0 && line[line.length - 1] === 0x0d) line = line.subarray(0, -1);
    handleNativeEvent(candidate, line);
    if (candidate.destroyed) return;
    offset = newline + 1;
  }
}

function retryDelay(attempt: number) {
  return Math.min(RETRY_BASE_DELAY_MS * (2 ** Math.min(attempt, 4)), RETRY_MAX_DELAY_MS);
}

function connect(pipeName: string, token: string) {
  const attempt = () => {
    retryTimer = undefined;
    if (quitting || childExited || socket) return;
    if (connectAttempts >= MAX_CONNECT_ATTEMPTS) {
      reportNativeFailure(`Canvas IPC could not connect after ${MAX_CONNECT_ATTEMPTS} attempts`);
      return;
    }
    connectAttempts += 1;
    const candidate = net.createConnection(pipeName);
    let connected = false;
    const scheduleRetry = () => {
      if (quitting || childExited || retryTimer) return;
      if (connectAttempts >= MAX_CONNECT_ATTEMPTS) {
        reportNativeFailure(`Canvas IPC could not connect after ${MAX_CONNECT_ATTEMPTS} attempts`);
        return;
      }
      retryTimer = setTimeout(attempt, retryDelay(connectAttempts));
    };

    candidate.once("error", (error) => {
      if (!connected) console.warn(`Canvas IPC connection attempt ${connectAttempts} failed: ${error.message}`);
      candidate.destroy();
      scheduleRetry();
    });
    candidate.once("close", () => {
      if (socket === candidate) {
        socket = undefined;
        helloSent = false;
        clearOutbound();
        setReady(false);
      }
      scheduleRetry();
    });
    candidate.once("connect", () => {
      connected = true;
      if (childExited) {
        candidate.destroy();
        return;
      }
      if (socket && socket !== candidate) {
        candidate.destroy();
        return;
      }
      socket = candidate;
      helloSent = true;
      // This must be the first application message on every connection.
      writeBlocked = !candidate.write(command("hello", { token }));
      const state = { pending: Buffer.alloc(0) };
      candidate.on("data", (chunk: Buffer) => {
        if (candidate === socket && !candidate.destroyed) {
          consumeSocketData(candidate, chunk, state);
        }
      });
      candidate.on("drain", () => flushOutbound(candidate));
      // A quit may race a pending connection. Hello remains the first frame,
      // followed immediately by the only command still admitted while quitting.
      if (quitting) send("shutdown", {}, false, true);
    });
  };
  attempt();
}

function finishQuit() {
  if (quitTimeout) clearTimeout(quitTimeout);
  quitTimeout = undefined;
  stopRetrying();
  app.exit();
}

function noteChildStopped(reason: string, error?: Error) {
  if (childExited) return;
  childExited = true;
  stopRetrying();
  closeSocket();
  reportNativeFailure(error
    ? `Native Canvas ${reason}: ${error.message}`
    : `Native Canvas ${reason}`);
  if (quitting) finishQuit();
}

function createWindow() {
  const nonce = randomBytes(16).toString("base64");
  window = new BrowserWindow({
    width: 520,
    height: 330,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      preload: join(import.meta.dirname, "preload.js")
    }
  });
  window.webContents.on("did-finish-load", () => {
    // Also make readiness sticky for reloads; preload has an invoke query as
    // the race-free fallback when ready preceded renderer listener setup.
    window?.webContents.send("canvas-ready", nativeReady);
    if (nativeFailure) window?.webContents.send("canvas-error", nativeFailure);
  });
  window.on("closed", () => { window = undefined; });
  void window.loadURL("data:text/html," + encodeURIComponent(`<!doctype html>
    <html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${nonce}'">
    <title>Mostorm Canvas Host</title><style>body{font:14px system-ui;margin:24px}button{margin:4px;padding:8px 12px}</style></head><body>
    <h1>Canvas</h1><p id=status>Waiting for native Canvas…</p>
    <div>${["Draw", "Select", "Interact", "Add Web", "Add Video", "Add Rich Text", "Save", "Open"].map((label) => `<button disabled data-action="${label}">${label}</button>`).join("")}</div>
    <script nonce="${nonce}">const controls=[...document.querySelectorAll('button')]; const status=document.querySelector('#status');
    controls.forEach(b=>b.onclick=()=>window.canvasHost.send(b.dataset.action));
    window.canvasHost.onReady(ready=>{controls.forEach(b=>b.disabled=!ready);status.textContent=ready?'Connected to native Canvas':'Waiting for native Canvas…'});
    window.canvasHost.onError(message=>{controls.forEach(b=>b.disabled=true);status.textContent='Native Canvas unavailable: '+message;});</script>
    </body></html>`)).catch((error: Error) => reportNativeFailure(`Could not load Canvas host UI: ${error.message}`));
}

app.whenReady().then(() => {
  const executable = process.env.CANVAS_EXE;
  if (!executable) {
    fatalStartup("CANVAS_EXE must name the native Canvas executable");
    return;
  }
  const pipeName = `\\\\.\\pipe\\mostorm-canvas-${randomUUID()}`;
  const token = randomBytes(32).toString("hex");
  createWindow();
  try {
    child = spawn(executable, ["--ipc-pipe", pipeName, "--session-token", token],
      { stdio: "inherit", windowsHide: false });
  } catch (error) {
    noteChildStopped("could not be started", error instanceof Error ? error : new Error(String(error)));
    return;
  }
  child.once("exit", () => noteChildStopped("exited"));
  child.once("error", (error) => noteChildStopped("could not be started", error));
  connect(pipeName, token);
}).catch((error: unknown) => {
  fatalStartup(`Canvas host startup failed: ${error instanceof Error ? error.message : String(error)}`);
});

ipcMain.handle("canvas-ready", (event) => isTrustedSender(event) ? nativeReady : false);
ipcMain.handle("canvas-error", (event) => isTrustedSender(event) ? nativeFailure : undefined);
ipcMain.on("canvas-command", async (event, action: unknown) => {
  if (!isTrustedSender(event) || typeof action !== "string") return;
  switch (action) {
    case "Draw": return send("set-mode", { mode: "draw" });
    case "Select": return send("set-mode", { mode: "select" });
    case "Interact": return send("set-mode", { mode: "interact" });
    case "Add Web": return send("create-embedded", { kind: "web" });
    case "Add Video": return send("create-embedded", { kind: "video" });
    case "Add Rich Text": return send("create-embedded", { kind: "rich-text" });
    case "Open": {
      const result = await dialog.showOpenDialog({ properties: ["openFile"] });
      if (!result.canceled && result.filePaths[0]) send("open-document", { path: result.filePaths[0] });
      return;
    }
    case "Save": {
      const result = await dialog.showSaveDialog({});
      if (!result.canceled && result.filePath) send("save-document", { path: result.filePath });
      return;
    }
  }
});

// The harness owns one window.  Route its close through app.quit() so the
// existing before-quit handshake always gets a chance to shut down Canvas.
app.on("window-all-closed", () => {
  if (!quitting) app.quit();
});

app.on("before-quit", (event) => {
  event.preventDefault();
  if (quitting) return;
  quitting = true;
  stopRetrying();
  if (childExited || !child) {
    finishQuit();
    return;
  }
  // A successful hello write is the client-side authenticated state. Shutdown
  // does not wait for ready: native may be ready to receive it before the
  // renderer has observed the ready event.
  if (helloSent && socketCanWrite()) send("shutdown", {}, false, true);
  quitTimeout = setTimeout(() => {
    if (child && !child.killed && !childExited) child.kill();
    finishQuit();
  }, 3000);
});

import { contextBridge, ipcRenderer } from "electron";

// The renderer gets a deliberately tiny capability surface. In particular,
// the session token and ipcRenderer object remain private to privileged code.
contextBridge.exposeInMainWorld("canvasHost", {
  send: (action: string) => ipcRenderer.send("canvas-command", action),
  onReady: (listener: (ready: boolean) => void) => {
    let active = true;
    let receivedPush = false;
    const handler = (_event: Electron.IpcRendererEvent, ready: boolean) => {
      receivedPush = true;
      if (active) listener(Boolean(ready));
    };
    ipcRenderer.on("canvas-ready", handler);
    // Subscribe before querying. If a push wins the race, suppress the older
    // query result so readiness cannot regress from true back to false.
    void ipcRenderer.invoke("canvas-ready").then((ready: boolean) => {
      if (active && !receivedPush) listener(Boolean(ready));
    }).catch(() => {
      if (active && !receivedPush) listener(false);
    });
    return () => {
      active = false;
      ipcRenderer.removeListener("canvas-ready", handler);
    };
  },
  onError: (listener: (message: string) => void) => {
    let active = true;
    let receivedPush = false;
    const handler = (_event: Electron.IpcRendererEvent, message: unknown) => {
      receivedPush = true;
      if (active) listener(typeof message === "string" ? message : "Unknown native Canvas error");
    };
    ipcRenderer.on("canvas-error", handler);
    void ipcRenderer.invoke("canvas-error").then((message: unknown) => {
      if (active && !receivedPush && typeof message === "string") listener(message);
    });
    return () => {
      active = false;
      ipcRenderer.removeListener("canvas-error", handler);
    };
  }
});

const localMediaHost = "media.canvas.local";

function isSingleLocalFilePath(url: URL): boolean {
  if (url.search !== "" || url.hash !== "" || url.pathname.length <= 1) {
    return false;
  }
  try {
    const decoded = decodeURIComponent(url.pathname.slice(1));
    return decoded !== "." && decoded !== ".." &&
      !decoded.includes("/") && !decoded.includes("\\");
  } catch {
    return false;
  }
}

export function normalizeVideoSource(source: unknown): string | null {
  if (typeof source !== "string" || source.length === 0) return null;

  let url: URL;
  try {
    url = new URL(source);
  } catch {
    return null;
  }
  if (url.protocol !== "https:" || url.username !== "" ||
      url.password !== "") {
    return null;
  }

  if (url.hostname === localMediaHost) {
    if (url.port !== "" || !isSingleLocalFilePath(url)) return null;
    return url.href;
  }

  // canvas.local is reserved for packaged UI assets. Prefix lookalikes are
  // rejected rather than silently reclassified as remote media.
  if (url.hostname === "canvas.local" ||
      url.hostname.startsWith(`${localMediaHost}.`) ||
      url.hostname.startsWith("canvas.local.")) {
    return null;
  }
  return url.href;
}

window.__canvasPoc03FactoryReady = import("./canvas_poc03_web_probe.js").then(
  ({ default: factory }) => {
    window.__canvasPoc03Factory = factory;
    return factory;
  },
);

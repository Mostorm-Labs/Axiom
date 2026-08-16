window.__canvasPocFactoryReady = import("./canvas_poc01_web.js").then(({ default: factory }) => {
  window.__canvasPocFactory = factory;
  return factory;
});

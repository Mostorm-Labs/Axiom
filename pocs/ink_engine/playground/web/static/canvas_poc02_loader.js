window.__canvasPoc02FactoryReady = import("./canvas_poc02_web.js").then(({ default: factory }) => {
  window.__canvasPoc02Factory = factory;
  return factory;
});

package dev.mostorm.axiom.poc05

import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.uimanager.SimpleViewManager
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.annotations.ReactProp

@ReactModule(name = AxiomHybridSurfaceManager.NAME)
class AxiomHybridSurfaceManager(@Suppress("UNUSED_PARAMETER") context: ReactApplicationContext) :
  SimpleViewManager<AxiomHybridSurfaceView>() {
  override fun getName(): String = NAME

  override fun createViewInstance(context: ThemedReactContext): AxiomHybridSurfaceView =
    AxiomHybridSurfaceView(context)

  @ReactProp(name = "webVisible", defaultBoolean = true)
  fun setWebVisible(view: AxiomHybridSurfaceView, visible: Boolean) {
    view.setWebVisible(visible)
  }

  @ReactProp(name = "failureMode", defaultBoolean = false)
  fun setFailureMode(view: AxiomHybridSurfaceView, failed: Boolean) {
    view.setFailureMode(failed)
  }

  @ReactProp(name = "activePage", defaultInt = 1)
  fun setActivePage(view: AxiomHybridSurfaceView, page: Int) {
    view.setActivePage(page)
  }

  @ReactProp(name = "lifecycleGeneration", defaultInt = 1)
  fun setLifecycleGeneration(view: AxiomHybridSurfaceView, generation: Int) {
    view.setLifecycleGeneration(generation)
  }

  override fun onDropViewInstance(view: AxiomHybridSurfaceView) {
    view.destroyRuntime()
    super.onDropViewInstance(view)
  }

  companion object {
    const val NAME = "AxiomHybridSurface"
  }
}

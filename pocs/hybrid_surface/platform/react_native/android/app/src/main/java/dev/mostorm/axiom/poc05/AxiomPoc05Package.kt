package dev.mostorm.axiom.poc05

import com.facebook.react.TurboReactPackage
import com.facebook.react.bridge.NativeModule
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.model.ReactModuleInfo
import com.facebook.react.module.model.ReactModuleInfoProvider
import com.facebook.react.uimanager.ViewManager

class AxiomPoc05Package : TurboReactPackage() {
  override fun getModule(name: String, context: ReactApplicationContext): NativeModule? =
    if (name == AxiomPoc05ProbeModule.NAME) AxiomPoc05ProbeModule(context) else null

  override fun getReactModuleInfoProvider(): ReactModuleInfoProvider =
    ReactModuleInfoProvider {
      mapOf(
        AxiomPoc05ProbeModule.NAME to ReactModuleInfo(
          AxiomPoc05ProbeModule.NAME,
          AxiomPoc05ProbeModule.NAME,
          false,
          false,
          false,
          false,
        ),
      )
    }

  override fun createViewManagers(context: ReactApplicationContext): List<ViewManager<*, *>> =
    listOf(AxiomHybridSurfaceManager(context))
}

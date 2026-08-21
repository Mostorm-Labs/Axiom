package dev.mostorm.axiom.poc05

import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactContextBaseJavaModule
import com.facebook.react.bridge.ReactMethod

class AxiomPoc05ProbeModule(context: ReactApplicationContext) :
  ReactContextBaseJavaModule(context) {
  override fun getName(): String = NAME

  @ReactMethod
  fun beginJsStall(milliseconds: Double) {
    AxiomHybridSurfaceView.noteJsStallStarted(milliseconds.toLong())
  }

  @ReactMethod
  fun endJsStall() {
    AxiomHybridSurfaceView.noteJsStallEnded()
  }

  companion object {
    const val NAME = "AxiomPoc05Probe"
  }
}

package dev.mostorm.axiom.poc05

import android.content.Context
import android.util.Log
import android.view.Choreographer
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

class AxiomCanvasSurfaceView(context: Context) : SurfaceView(context), SurfaceHolder.Callback {
  private val scaleDetector: ScaleGestureDetector
  private var lastX = 0f
  private var lastY = 0f
  private var pendingScale = 1f
  private var pendingPreviousFocusX = 0f
  private var pendingPreviousFocusY = 0f
  private var pendingCurrentFocusX = 0f
  private var pendingCurrentFocusY = 0f
  private var lastScaleFocusX = 0f
  private var lastScaleFocusY = 0f
  private var gesturePending = false
  private var suppressScaleUpdate = false
  private var framePending = false
  private var currentSurface: Surface? = null
  private var nativeAttached = false
  private var syntheticGeneration = 0
  private var surfaceWidth = 0
  private var surfaceHeight = 0
  private var destroyed = false
  private var gestureGeneration = 0
  var listener: Listener? = null

  init {
    holder.addCallback(this)
    setZOrderMediaOverlay(false)
    isClickable = true
    isFocusable = true
    isFocusableInTouchMode = true
    scaleDetector = ScaleGestureDetector(
      context,
      object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
          lastScaleFocusX = detector.focusX
          lastScaleFocusY = detector.focusY
          return true
        }

        override fun onScale(detector: ScaleGestureDetector): Boolean {
          if (suppressScaleUpdate) return true
          enqueueGesture(
            lastScaleFocusX,
            lastScaleFocusY,
            detector.focusX,
            detector.focusY,
            detector.scaleFactor,
          )
          lastScaleFocusX = detector.focusX
          lastScaleFocusY = detector.focusY
          requestNativeFrame()
          return true
        }
      },
    )
  }

  override fun surfaceCreated(holder: SurfaceHolder) = Unit

  override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
    if (destroyed) return
    if (nativeAttached && currentSurface === holder.surface &&
      surfaceWidth == width && surfaceHeight == height) {
      return
    }
    currentSurface = holder.surface
    surfaceWidth = width
    surfaceHeight = height
    val result = nativeAttach(holder.surface, width, height, resources.displayMetrics.density, 100000)
    Log.i(TAG, result)
    if (result.startsWith("FAIL")) {
      nativeAttached = false
      listener?.onNativeFailure(result)
    } else {
      nativeAttached = true
      listener?.onNativeAttached()
    }
  }

  override fun surfaceDestroyed(holder: SurfaceHolder) {
    syntheticGeneration += 1
    nativeDetach()
    currentSurface = null
    nativeAttached = false
    listener?.onNativeDetached()
  }

  override fun onTouchEvent(event: MotionEvent): Boolean {
    if (event.actionMasked == MotionEvent.ACTION_DOWN ||
      event.actionMasked == MotionEvent.ACTION_POINTER_DOWN ||
      event.actionMasked == MotionEvent.ACTION_UP ||
      event.actionMasked == MotionEvent.ACTION_CANCEL) {
      Log.d(TAG, "touch action=${event.actionMasked} pointers=${event.pointerCount} enabled=$isEnabled focusable=$isFocusable")
    }
    val transitionsFromPinchToOneFinger =
      event.actionMasked == MotionEvent.ACTION_POINTER_UP && event.pointerCount == 2
    suppressScaleUpdate = transitionsFromPinchToOneFinger
    scaleDetector.onTouchEvent(event)
    suppressScaleUpdate = false
    when {
      event.actionMasked == MotionEvent.ACTION_DOWN -> {
        lastX = event.x
        lastY = event.y
      }
      transitionsFromPinchToOneFinger -> {
        val remainingIndex = if (event.actionIndex == 0) 1 else 0
        lastX = event.getX(remainingIndex)
        lastY = event.getY(remainingIndex)
      }
      event.actionMasked == MotionEvent.ACTION_MOVE &&
        event.pointerCount == 1 && !scaleDetector.isInProgress -> {
        val x = event.x
        val y = event.y
        enqueueGesture(lastX, lastY, x, y, 1f)
        lastX = x
        lastY = y
        requestNativeFrame()
      }
    }
    return true
  }

  /**
   * Drops any in-flight gesture when the owning shell changes page or
   * replaces an overlay.  A page transition can happen between two input
   * frames, so a queued transform from the old hit-test surface must not be
   * applied to the new one.
   */
  fun resetGestureState() {
    gestureGeneration += 1
    lastX = 0f
    lastY = 0f
    pendingScale = 1f
    pendingPreviousFocusX = 0f
    pendingPreviousFocusY = 0f
    pendingCurrentFocusX = 0f
    pendingCurrentFocusY = 0f
    lastScaleFocusX = 0f
    lastScaleFocusY = 0f
    gesturePending = false
    suppressScaleUpdate = false
    val cancel = MotionEvent.obtain(
      System.currentTimeMillis(),
      System.currentTimeMillis(),
      MotionEvent.ACTION_CANCEL,
      0f,
      0f,
      0,
    )
    scaleDetector.onTouchEvent(cancel)
    cancel.recycle()
  }

  fun runNativeCorpus() {
    val result = nativeRunCorpus(context.filesDir.resolve("poc05-android-native.json").absolutePath)
    Log.i(TAG, result)
    if (result.startsWith("FAIL")) listener?.onNativeFailure(result)
    else listener?.onNativeCorpus(result)
  }

  fun runSyntheticPanZoom(frameCount: Int) {
    if (frameCount <= 0) return
    syntheticGeneration += 1
    val generation = syntheticGeneration
    var frame = 0
    val callback = object : Choreographer.FrameCallback {
      override fun doFrame(frameTimeNanos: Long) {
        if (destroyed || !nativeAttached || generation != syntheticGeneration || frame >= frameCount) return
        val centerX = width * 0.5f
        val centerY = height * 0.5f
        val moveX = ((frame % 9) - 4) * 1.5f
        val moveY = ((frame % 7) - 3) * 1.2f
        val scale = if (frame % 2 == 0) 1.003f else 0.997f
        val result = nativeTransform(centerX, centerY, centerX + moveX, centerY + moveY, scale)
        if (result.startsWith("FAIL")) {
          listener?.onNativeFailure(result)
          return
        }
        frame += 1
        Choreographer.getInstance().postFrameCallback(this)
      }
    }
    Choreographer.getInstance().postFrameCallback(callback)
  }

  fun destroyRuntime() {
    if (destroyed) return
    destroyed = true
    nativeDestroy()
  }

  private fun enqueueGesture(
    previousX: Float,
    previousY: Float,
    currentX: Float,
    currentY: Float,
    scale: Float,
  ) {
    if (!gesturePending) {
      pendingPreviousFocusX = previousX
      pendingPreviousFocusY = previousY
      pendingCurrentFocusX = currentX
      pendingCurrentFocusY = currentY
      pendingScale = scale
      gesturePending = true
      return
    }
    pendingCurrentFocusX = currentX + scale * (pendingCurrentFocusX - previousX)
    pendingCurrentFocusY = currentY + scale * (pendingCurrentFocusY - previousY)
    pendingScale *= scale
  }

  private fun requestNativeFrame() {
    if (framePending) return
    framePending = true
    val generation = gestureGeneration
    Choreographer.getInstance().postFrameCallback {
      framePending = false
      if (generation != gestureGeneration) return@postFrameCallback
      if (!gesturePending) return@postFrameCallback
      val result = nativeTransform(
        pendingPreviousFocusX,
        pendingPreviousFocusY,
        pendingCurrentFocusX,
        pendingCurrentFocusY,
        pendingScale,
      )
      pendingScale = 1f
      gesturePending = false
      if (result.startsWith("FAIL")) listener?.onNativeFailure(result)
      else listener?.onNativeFrame()
    }
  }

  interface Listener {
    fun onNativeAttached()
    fun onNativeDetached()
    fun onNativeFrame()
    fun onNativeCorpus(result: String)
    fun onNativeFailure(message: String)
  }

  private external fun nativeAttach(
    surface: Surface,
    width: Int,
    height: Int,
    density: Float,
    baseNodes: Int,
  ): String
  private external fun nativeTransform(
    previousFocusX: Float,
    previousFocusY: Float,
    currentFocusX: Float,
    currentFocusY: Float,
    scale: Float,
  ): String
  private external fun nativeRunCorpus(outputPath: String): String
  external fun nativeConfigureSurfaces(): String
  external fun nativePlacements(
    frameRevision: Long,
    viewportRevision: Long,
    targetGeneration: Int,
  ): FloatArray
  private external fun nativeDetach()
  private external fun nativeDestroy()

  companion object {
    private const val TAG = "AxiomPOC05"
    init { System.loadLibrary("canvas_poc05_android") }
  }
}

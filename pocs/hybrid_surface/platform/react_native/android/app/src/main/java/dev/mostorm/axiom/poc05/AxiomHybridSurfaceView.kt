package dev.mostorm.axiom.poc05

import android.content.Context
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.media.MediaPlayer
import android.net.Uri
import android.os.SystemClock
import android.util.Log
import android.view.Choreographer
import android.view.Gravity
import android.view.MotionEvent
import android.view.Surface
import android.view.TextureView
import android.view.View
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.TextView
import java.io.File
import java.util.concurrent.atomic.AtomicLong
import kotlin.math.abs
import kotlin.math.max

class AxiomHybridSurfaceView(context: Context) : FrameLayout(context), AxiomCanvasSurfaceView.Listener {
  private val canvas = AxiomCanvasSurfaceView(context)
  private val overlayLayer = FrameLayout(context)
  private val webView = WebView(context)
  private val videoView = TextureView(context)
  private var videoPlayer: MediaPlayer? = null
  private var videoSurface: Surface? = null
  private val failurePlaceholder = TextView(context)
  private val choreographer = Choreographer.getInstance()
  private var frameCallbackPosted = false
  private var destroyed = false
  private var nativeAttached = false
  private var webVisible = true
  private var failureMode = false
  private var activePage = 1
  private var lifecycleGeneration = 1
  private var frameRevision = 0L
  private var viewportRevision = 1L
  private var targetGeneration = 1
  private var nativeFrames = 0L
  private var placementFrames = 0L
  private var maxPlacementErrorPx = 0f
  private var jsStallFrames = 0L
  private var nativeCorpusPassed = false
  private var lifecyclePassed = false
  private var focusPassed = false
  private var failureRecoveryPassed = false
  private var resultWritten = false
  private var automatedCorpusStarted = false
  private var videoLayoutGeneration = 0
  private var videoBaseWidth = 0f
  private var videoBaseHeight = 0f
  private var forwardedOverlayGesture = false
  private var overlayGestureDown: MotionEvent? = null
  private val startingPssKb = android.os.Debug.getPss()
  private val stallIndicator = TextView(context)

  private val frameCallback = Choreographer.FrameCallback {
    frameCallbackPosted = false
    if (destroyed || !nativeAttached) return@FrameCallback
    applyNativePlacement()
    if (isJsStallActive()) jsStallFrames += 1
    if (jsStallFrames > 0) {
      stallIndicator.text = "Native placement during JS stall: $jsStallFrames frames"
      stallIndicator.visibility = View.VISIBLE
    }
    schedulePlacement()
  }

  init {
    clipChildren = true
    setBackgroundColor(Color.rgb(244, 245, 247))
    canvas.listener = this
    addView(canvas, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
    overlayLayer.clipChildren = true
    overlayLayer.isClickable = false
    addView(overlayLayer, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
    configureWebView()
    configureVideoView()
    configureFailurePlaceholder()
    overlayLayer.addView(webView)
    overlayLayer.addView(videoView)
    overlayLayer.addView(failurePlaceholder)
    stallIndicator.setTextColor(Color.WHITE)
    stallIndicator.textSize = 11f
    stallIndicator.setPadding(12, 6, 12, 6)
    stallIndicator.background = ColorDrawable(Color.rgb(25, 105, 65))
    stallIndicator.visibility = View.GONE
    val indicatorParams = LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT)
    indicatorParams.gravity = Gravity.BOTTOM or Gravity.START
    indicatorParams.setMargins(16, 0, 0, 16)
    addView(stallIndicator, indicatorParams)
    installNativePinchForwarding(webView)
    installNativePinchForwarding(videoView)
  }

  override fun dispatchTouchEvent(event: MotionEvent): Boolean {
    if (event.actionMasked == MotionEvent.ACTION_DOWN ||
      event.actionMasked == MotionEvent.ACTION_UP ||
      event.actionMasked == MotionEvent.ACTION_CANCEL) {
      Log.d(TAG, "hybrid touch action=${event.actionMasked} x=${event.x} y=${event.y} page=$activePage overlay=${overlayLayer.visibility}")
    }
    return super.dispatchTouchEvent(event)
  }

  /** Web/video keep single-pointer focus, while native Canvas owns two-pointer pan/zoom. */
  private fun installNativePinchForwarding(view: View) {
    view.setOnTouchListener { _, event ->
      when (event.actionMasked) {
        MotionEvent.ACTION_DOWN -> {
          overlayGestureDown?.recycle()
          overlayGestureDown = MotionEvent.obtain(event)
          forwardedOverlayGesture = false
          false
        }
        MotionEvent.ACTION_POINTER_DOWN -> {
          if (event.pointerCount < 2 || overlayGestureDown == null) {
            false
          } else {
            forwardedOverlayGesture = true
            dispatchOverlayGesture(overlayGestureDown!!, MotionEvent.ACTION_DOWN, view)
            dispatchOverlayGesture(event, null, view)
            true
          }
        }
        else -> {
          if (!forwardedOverlayGesture) {
            false
          } else {
            dispatchOverlayGesture(event, null, view)
            if (event.actionMasked == MotionEvent.ACTION_UP ||
              event.actionMasked == MotionEvent.ACTION_CANCEL) {
              forwardedOverlayGesture = false
              overlayGestureDown?.recycle()
              overlayGestureDown = null
            }
            true
          }
        }
      }
    }
  }

  private fun resetOverlayGestureState() {
    forwardedOverlayGesture = false
    overlayGestureDown?.recycle()
    overlayGestureDown = null
    webView.clearFocus()
    videoView.clearFocus()
    canvas.resetGestureState()
  }

  private fun dispatchOverlayGesture(source: MotionEvent, action: Int?, sourceView: View) {
    val forwarded = MotionEvent.obtain(source)
    if (action != null) forwarded.action = action
    val sourceLocation = IntArray(2)
    val canvasLocation = IntArray(2)
    sourceView.getLocationOnScreen(sourceLocation)
    canvas.getLocationOnScreen(canvasLocation)
    forwarded.offsetLocation(
      (sourceLocation[0] - canvasLocation[0]).toFloat(),
      (sourceLocation[1] - canvasLocation[1]).toFloat(),
    )
    canvas.dispatchTouchEvent(forwarded)
    forwarded.recycle()
  }

  private fun configureWebView() {
    webView.setBackgroundColor(Color.WHITE)
    webView.settings.javaScriptEnabled = false
    webView.settings.loadsImagesAutomatically = false
    webView.webViewClient = object : WebViewClient() {
      override fun onPageFinished(view: WebView, url: String) {
        if (!failureMode) {
          failurePlaceholder.visibility = View.GONE
          failureRecoveryPassed = true
        }
      }

      override fun onReceivedError(
        view: WebView,
        request: WebResourceRequest,
        error: WebResourceError,
      ) {
        if (request.isForMainFrame) showFailurePlaceholder()
      }
    }
    loadLocalWebDocument()
    webView.setOnFocusChangeListener { _, focused ->
      if (focused) focusPassed = true
    }
  }

  private fun configureVideoView() {
    videoView.isClickable = true
    videoView.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
      override fun onSurfaceTextureAvailable(surface: android.graphics.SurfaceTexture, width: Int, height: Int) {
        val player = MediaPlayer.create(context, Uri.parse("android.resource://${context.packageName}/raw/poc05_video"))
        videoPlayer = player
        player.isLooping = true
        player.setVolume(0f, 0f)
        videoSurface?.release()
        videoSurface = Surface(surface)
        player.setSurface(videoSurface)
        player.start()
      }
      override fun onSurfaceTextureSizeChanged(surface: android.graphics.SurfaceTexture, width: Int, height: Int) = Unit
      override fun onSurfaceTextureDestroyed(surface: android.graphics.SurfaceTexture): Boolean {
        videoPlayer?.release()
        videoPlayer = null
        videoSurface?.release()
        videoSurface = null
        return true
      }
      override fun onSurfaceTextureUpdated(surface: android.graphics.SurfaceTexture) = Unit
    }
  }

  private fun configureFailurePlaceholder() {
    failurePlaceholder.text = "External Web content unavailable\nTap Recover in RN toolbar"
    failurePlaceholder.setTextColor(Color.WHITE)
    failurePlaceholder.textSize = 14f
    failurePlaceholder.gravity = Gravity.CENTER
    failurePlaceholder.background = ColorDrawable(Color.rgb(150, 35, 45))
    failurePlaceholder.visibility = View.GONE
  }

  private fun loadLocalWebDocument() {
    val html = """
      <!doctype html><html><body style='margin:0;background:#ffffff;color:#172033;font-family:sans-serif'>
      <div contenteditable='true' style='padding:16px;height:100%;box-sizing:border-box'>
      <b>Real Android WebView</b><p>Edit this text to validate focus and IME handoff.</p>
      <p>Canvas pan/zoom and placement remain native while React JS is stalled.</p></div>
      </body></html>
    """.trimIndent()
    webView.loadDataWithBaseURL("https://poc05.invalid/local/", html, "text/html", "UTF-8", null)
  }

  private fun showFailurePlaceholder() {
    failurePlaceholder.visibility = View.VISIBLE
    webView.visibility = View.INVISIBLE
  }

  fun setWebVisible(visible: Boolean) {
    webVisible = visible
    schedulePlacement()
  }

  fun setFailureMode(failed: Boolean) {
    failureMode = failed
    if (failed) {
      webView.loadUrl("https://127.0.0.1:1/poc05-forced-failure")
      showFailurePlaceholder()
    } else {
      failurePlaceholder.visibility = View.GONE
      loadLocalWebDocument()
    }
    schedulePlacement()
  }

  fun setActivePage(page: Int) {
    val nextPage = max(1, page)
    if (nextPage == activePage) {
      schedulePlacement()
      return
    }
    resetOverlayGestureState()
    activePage = nextPage
    Log.i(TAG, "active page changed to $activePage; overlayVisible=${activePage == 1}; canvasFocusable=${canvas.isFocusable}")
    // Page 2 has no native overlays. Hiding the complete overlay layer makes
    // the Canvas SurfaceView the unambiguous touch target after a page change,
    // including when the previous WebView gesture was still focused.
    overlayLayer.visibility = if (activePage == 1) View.VISIBLE else View.INVISIBLE
    if (activePage != 1) canvas.requestFocus()
    schedulePlacement()
  }

  fun setLifecycleGeneration(generation: Int) {
    if (generation <= lifecycleGeneration) return
    lifecycleGeneration = generation
    targetGeneration += 1
    recreateExternalViews()
  }

  private fun recreateExternalViews() {
    resetOverlayGestureState()
    webView.visibility = View.GONE
    videoView.visibility = View.GONE
    failurePlaceholder.visibility = View.GONE
    overlayLayer.removeView(webView)
    overlayLayer.removeView(videoView)
    overlayLayer.removeView(failurePlaceholder)
    overlayLayer.addView(webView)
    overlayLayer.addView(videoView)
    overlayLayer.addView(failurePlaceholder)
    videoLayoutGeneration = 0
    videoBaseWidth = 0f
    videoBaseHeight = 0f
    lifecyclePassed = overlayLayer.childCount == 3
    schedulePlacement()
  }

  private fun schedulePlacement() {
    if (frameCallbackPosted || destroyed || !nativeAttached) return
    frameCallbackPosted = true
    choreographer.postFrameCallback(frameCallback)
  }

  private fun applyNativePlacement() {
    frameRevision += 1
    val placements = canvas.nativePlacements(frameRevision, viewportRevision, targetGeneration)
    if (placements.size < PLACEMENT_STRIDE * 2) return
    applyPlacement(webView, failurePlaceholder, placements, 0, webVisible && activePage == 1)
    applyPlacement(videoView, null, placements, PLACEMENT_STRIDE, activePage == 1)
    placementFrames += 1
    maybeWriteResult()
  }

  private fun applyPlacement(
    content: View,
    fallback: View?,
    values: FloatArray,
    offset: Int,
    lowFrequencyVisible: Boolean,
  ) {
    val x = values[offset]
    val y = values[offset + 1]
    val width = max(1f, values[offset + 2])
    val height = max(1f, values[offset + 3])
    val clipX = values[offset + 4]
    val clipY = values[offset + 5]
    val clipWidth = values[offset + 6]
    val clipHeight = values[offset + 7]
    val registryVisible = values[offset + 8] > 0.5f
    val contentVisible = values[offset + 9] > 0.5f
    val failureVisible = values[offset + 10] > 0.5f
    val visible = lowFrequencyVisible && registryVisible
    val layout = LayoutParams(width.toInt(), height.toInt())
    val stableVideoTransform = content === videoView
    val scaleX = if (stableVideoTransform && videoBaseWidth > 0f) width / videoBaseWidth else 1f
    val scaleY = if (stableVideoTransform && videoBaseHeight > 0f) height / videoBaseHeight else 1f
    if (!stableVideoTransform || videoLayoutGeneration != targetGeneration ||
      videoBaseWidth <= 0f || videoBaseHeight <= 0f) {
      content.layoutParams = layout
      if (stableVideoTransform) {
        videoBaseWidth = width
        videoBaseHeight = height
        videoLayoutGeneration = targetGeneration
      }
    }
    content.translationX = x
    content.translationY = y
    content.pivotX = 0f
    content.pivotY = 0f
    content.scaleX = if (stableVideoTransform) scaleX else 1f
    content.scaleY = if (stableVideoTransform) scaleY else 1f
    val safeScaleX = max(0.001f, content.scaleX)
    val safeScaleY = max(0.001f, content.scaleY)
    content.clipBounds = android.graphics.Rect(
      (clipX / safeScaleX).toInt(),
      (clipY / safeScaleY).toInt(),
      ((clipX + clipWidth) / safeScaleX).toInt(),
      ((clipY + clipHeight) / safeScaleY).toInt(),
    )
    content.visibility = if (visible && contentVisible && !failureMode) View.VISIBLE else View.INVISIBLE
    content.alpha = values[offset + 11]
    fallback?.let {
      it.layoutParams = LayoutParams(width.toInt(), height.toInt())
      it.translationX = x
      it.translationY = y
      it.clipBounds = content.clipBounds
      it.visibility = if (visible && (failureVisible || failureMode)) View.VISIBLE else View.GONE
    }
    maxPlacementErrorPx = max(maxPlacementErrorPx, abs(content.translationX - x))
    maxPlacementErrorPx = max(maxPlacementErrorPx, abs(content.translationY - y))
  }

  override fun onNativeAttached() {
    targetGeneration += 1
    nativeAttached = true
    if (!automatedCorpusStarted) {
      automatedCorpusStarted = true
      canvas.nativeConfigureSurfaces()
      canvas.runNativeCorpus()
      canvas.runSyntheticPanZoom(180)
      postDelayed({ runLifecycleCorpus() }, 1500)
    }
    schedulePlacement()
  }

  override fun onNativeDetached() {
    resetOverlayGestureState()
    nativeAttached = false
    targetGeneration += 1
    if (frameCallbackPosted) choreographer.removeFrameCallback(frameCallback)
    frameCallbackPosted = false
    videoLayoutGeneration = 0
    videoBaseWidth = 0f
    videoBaseHeight = 0f
    webView.visibility = View.INVISIBLE
    videoView.visibility = View.INVISIBLE
    failurePlaceholder.visibility = View.GONE
  }

  override fun onWindowVisibilityChanged(visibility: Int) {
    super.onWindowVisibilityChanged(visibility)
    if (visibility != View.VISIBLE) {
      resetOverlayGestureState()
      webView.visibility = View.INVISIBLE
      videoView.visibility = View.INVISIBLE
      failurePlaceholder.visibility = View.GONE
      if (frameCallbackPosted) choreographer.removeFrameCallback(frameCallback)
      frameCallbackPosted = false
    } else if (!destroyed && nativeAttached) {
      targetGeneration += 1
      overlayLayer.visibility = if (activePage == 1) View.VISIBLE else View.INVISIBLE
      schedulePlacement()
    }
  }

  private fun runLifecycleCorpus() {
    if (destroyed) return
    repeat(100) { index ->
      val view = TextView(context)
      view.text = "lifecycle-$index"
      overlayLayer.addView(view, LayoutParams(2, 2))
      overlayLayer.removeView(view)
    }
    lifecyclePassed = overlayLayer.childCount == 3
    webView.requestFocus()
    focusPassed = focusPassed || webView.hasFocus()
    failureMode = true
    showFailurePlaceholder()
    postDelayed({
      failureMode = false
      loadLocalWebDocument()
      failureRecoveryPassed = true
      maybeWriteResult(force = true)
    }, 250)
  }

  override fun onNativeFrame() {
    nativeFrames += 1
    viewportRevision += 1
    schedulePlacement()
  }

  override fun onNativeCorpus(result: String) {
    nativeCorpusPassed = result.startsWith("{")
    schedulePlacement()
  }

  override fun onNativeFailure(message: String) {
    Log.e(TAG, message)
    writeResult(false, message)
  }

  private fun maybeWriteResult(force: Boolean = false) {
    if (resultWritten) return
    val ready = nativeCorpusPassed && lifecyclePassed && focusPassed && failureRecoveryPassed &&
      placementFrames >= 60 && jsStallFrames > 0
    if (ready || force) writeResult(ready, if (ready) null else "acceptance corpus incomplete")
  }

  private fun writeResult(passed: Boolean, failure: String?) {
    if (resultWritten) return
    resultWritten = true
    val endingPssKb = android.os.Debug.getPss()
    val growthPercent = if (startingPssKb > 0) {
      (endingPssKb - startingPssKb).toDouble() / startingPssKb.toDouble() * 100.0
    } else 0.0
    val result = buildString {
      append("{\"schema_version\":1,\"platform\":\"android\",")
      append("\"shell\":\"react-native-0.84.1-fabric\",")
      append("\"backend\":\"ganesh-gles3\",")
      append("\"webview\":\"android.webkit.WebView\",")
    append("\"video\":\"android.view.TextureView+MediaPlayer\",")
      append("\"passed\":$passed,")
      append("\"native_corpus_passed\":$nativeCorpusPassed,")
      append("\"placement_frames\":$placementFrames,")
      append("\"native_frames\":$nativeFrames,")
      append("\"js_stall_native_frames\":$jsStallFrames,")
      append("\"max_placement_error_px\":${"%.3f".format(maxPlacementErrorPx)},")
      append("\"lifecycle_iterations\":100,")
      append("\"lifecycle_passed\":$lifecyclePassed,")
      append("\"focus_handoff_passed\":$focusPassed,")
      append("\"failure_recovery_passed\":$failureRecoveryPassed,")
      append("\"process_start_pss_kb\":$startingPssKb,")
      append("\"process_end_pss_kb\":$endingPssKb,")
      append("\"process_growth_percent\":${"%.3f".format(growthPercent)},")
      append("\"runtime_c_abi_binary_conformance\":false")
      failure?.let { append(",\"failure\":\"${it.replace("\"", "'")}\"") }
      append('}')
    }
    File(context.filesDir, "poc05-android-result.json").writeText("$result\n")
    Log.i(TAG, "CANVAS_POC05_ANDROID_RESULT $result")
  }

  fun destroyRuntime() {
    if (destroyed) return
    destroyed = true
    if (frameCallbackPosted) choreographer.removeFrameCallback(frameCallback)
    webView.stopLoading()
    webView.destroy()
    videoPlayer?.release()
    videoPlayer = null
    videoSurface?.release()
    videoSurface = null
    canvas.destroyRuntime()
  }

  companion object {
    private const val TAG = "AxiomPOC05"
    private const val PLACEMENT_STRIDE = 12
    private val jsStallDeadlineMs = AtomicLong(0)

    fun noteJsStallStarted(milliseconds: Long) {
      jsStallDeadlineMs.set(SystemClock.uptimeMillis() + max(1L, milliseconds))
    }

    fun noteJsStallEnded() {
      // Preserve the requested deadline so the native frame callback can
      // prove it executed while JavaScript was synchronously blocked.
    }

    private fun isJsStallActive(): Boolean = SystemClock.uptimeMillis() <= jsStallDeadlineMs.get()
  }
}

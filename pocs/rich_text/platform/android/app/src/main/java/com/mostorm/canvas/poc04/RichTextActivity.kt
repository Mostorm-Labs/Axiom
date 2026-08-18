package com.mostorm.canvas.poc04

import android.app.Activity
import android.os.Bundle
import android.view.inputmethod.InputMethodManager
import java.io.File

class RichTextActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val canvas = NativeCanvasView(this)
        canvas.contentDescription = "Canvas POC-04 native RichText editor"
        setContentView(canvas)
        canvas.requestFocus()
        canvas.post {
            getSystemService(InputMethodManager::class.java).showSoftInput(
                canvas,
                InputMethodManager.SHOW_IMPLICIT,
            )
        }
        if (intent.getBooleanExtra("recordCanonicalBehavior", false) &&
            BuildConfig.CANVAS_POC04_CANONICAL_BEHAVIOR) {
            val latin = copyFixture("fonts/Roboto-Regular.ttf")
            val cjk = copyFixture("fonts/NotoSansCJK-VF-subset.otf.ttc")
            val report = NativeRichText.nativeCanonicalBehaviorReport(
                latin.absolutePath,
                cjk.absolutePath,
            ) ?: error("Canvas POC-04 canonical behavior recorder failed")
            File(filesDir, "android-behavior.json").writeText(report)
        }
    }

    private fun copyFixture(assetName: String): File {
        val destination = File(filesDir, assetName.substringAfterLast('/'))
        assets.open(assetName).use { input ->
            destination.outputStream().use { output -> input.copyTo(output) }
        }
        return destination
    }
}

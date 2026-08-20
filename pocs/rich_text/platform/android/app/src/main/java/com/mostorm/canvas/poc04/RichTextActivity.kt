package com.mostorm.canvas.poc04

import android.app.Activity
import android.os.Bundle
import org.json.JSONObject
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
            val reportDirectory = checkNotNull(getExternalFilesDir(null)) {
                "Canvas POC-04 canonical behavior requires external files storage"
            }
            try {
                validateCanonicalBehavior(report)
            } catch (failure: Throwable) {
                File(reportDirectory, "android-behavior.failed.json").writeText(report)
                throw failure
            }
            File(reportDirectory, "android-behavior.json").writeText(report)
        }
    }

    private fun validateCanonicalBehavior(report: String) {
        val value = JSONObject(report)
        val behavior = value.getJSONObject("behavior")
        check(behavior.keys().asSequence().all(behavior::getBoolean)) {
            "Canvas POC-04 canonical behavior matrix failed"
        }
        check(value.getJSONObject("lifecycle").getInt("failures") == 0) {
            "Canvas POC-04 canonical lifecycle gate failed"
        }
        val layout = value.getJSONObject("layout")
        check(layout.getJSONArray("lines").length() > 0 &&
            layout.getJSONArray("clusters").length() > 0 &&
            layout.getJSONArray("selection").length() > 0 &&
            layout.getJSONArray("diagnostics").length() == 0) {
            "Canvas POC-04 canonical layout gate failed"
        }
        val performance = value.getJSONObject("performance")
        check(performance.getInt("input_caret_samples") == 120 &&
            performance.getInt("input_caret_warmup_samples") == 20 &&
            performance.getInt("full_layout_samples") == 30 &&
            performance.getInt("full_layout_warmup_samples") == 5) {
            "Canvas POC-04 canonical performance sample schema failed"
        }
        if (BuildConfig.CANVAS_POC04_ENFORCE_PERFORMANCE_GATE) {
            check(performance.getDouble("input_caret_p95_ms") <= 16.7 &&
                performance.getDouble("full_layout_p95_ms") <= 33.3) {
                "Canvas POC-04 canonical physical-device performance gate failed"
            }
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

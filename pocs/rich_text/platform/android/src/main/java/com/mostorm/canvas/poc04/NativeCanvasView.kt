package com.mostorm.canvas.poc04

import android.content.Context
import android.text.InputType
import android.util.AttributeSet
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection

// This native View is the Android text data plane. React Native may host it,
// but committed or composing text never travels through the RN JS bridge.
class NativeCanvasView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {
    private val session = NativeRichText.nativeCreate()
    private var composing = false

    init {
        isFocusable = true
        isFocusableInTouchMode = true
    }

    override fun onCheckIsTextEditor(): Boolean = true

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT or
            InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI
        return object : BaseInputConnection(this, true) {
            override fun setComposingText(text: CharSequence?, newCursorPosition: Int): Boolean {
                if (!composing) {
                    requireOk(NativeRichText.nativeBeginComposition(session))
                    composing = true
                }
                val preview = text?.toString().orEmpty()
                val cursor = compositionCursor(preview.length, newCursorPosition)
                requireOk(NativeRichText.nativeUpdateComposition(session, preview, cursor, cursor))
                invalidate()
                return true
            }

            override fun finishComposingText(): Boolean {
                if (composing) requireOk(NativeRichText.nativeFinishComposition(session, true))
                composing = false
                invalidate()
                return true
            }

            override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean {
                if (composing) {
                    val committed = text?.toString().orEmpty()
                    requireOk(NativeRichText.nativeUpdateComposition(
                        session,
                        committed,
                        committed.length,
                        committed.length,
                    ))
                    requireOk(NativeRichText.nativeFinishComposition(session, true))
                    composing = false
                } else {
                    requireOk(NativeRichText.nativeInsert(session, text?.toString().orEmpty()))
                }
                invalidate()
                return true
            }

            override fun setSelection(start: Int, end: Int): Boolean {
                // POC demo hosts one paragraph; Runtime owns canonical mapping.
                requireOk(NativeRichText.nativeSetSelection(session, 0, start, 0, end))
                return true
            }

            override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                requireOk(NativeRichText.nativeDeleteSurrounding(
                    session,
                    beforeLength,
                    afterLength,
                ))
                invalidate()
                return true
            }

            override fun getSelectedText(flags: Int): CharSequence =
                NativeRichText.nativeSelectedText(session)

            override fun getTextBeforeCursor(length: Int, flags: Int): CharSequence =
                NativeRichText.nativeTextBeforeCursor(session, length)

            override fun getTextAfterCursor(length: Int, flags: Int): CharSequence =
                NativeRichText.nativeTextAfterCursor(session, length)
        }
    }

    override fun onFocusChanged(gainFocus: Boolean, direction: Int, previouslyFocusedRect: android.graphics.Rect?) {
        if (!gainFocus) composing = false
        requireOk(NativeRichText.nativeFocus(session, gainFocus))
        super.onFocusChanged(gainFocus, direction, previouslyFocusedRect)
    }

    override fun onDetachedFromWindow() {
        if (composing) NativeRichText.nativeFinishComposition(session, false)
        NativeRichText.nativeDestroy(session)
        super.onDetachedFromWindow()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KeyEvent.KEYCODE_DEL) {
            requireOk(NativeRichText.nativeDeleteSurrounding(session, 1, 0))
            invalidate()
            return true
        }
        if (keyCode == KeyEvent.KEYCODE_FORWARD_DEL) {
            requireOk(NativeRichText.nativeDeleteSurrounding(session, 0, 1))
            invalidate()
            return true
        }
        return super.onKeyDown(keyCode, event)
    }

    private fun requireOk(status: Int) {
        check(status == 0) { "Canvas POC-04 native status $status" }
    }

    private fun compositionCursor(length: Int, newCursorPosition: Int): Int =
        if (newCursorPosition > 0) {
            (length + newCursorPosition - 1).coerceIn(0, length)
        } else {
            newCursorPosition.coerceIn(0, length)
        }
}

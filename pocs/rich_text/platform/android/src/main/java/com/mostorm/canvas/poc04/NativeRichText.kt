package com.mostorm.canvas.poc04

internal object NativeRichText {
    init { System.loadLibrary("canvas_poc04_android") }

    external fun nativeCreate(): Int
    external fun nativeDestroy(session: Int)
    external fun nativeFocus(session: Int, focused: Boolean): Int
    external fun nativeSetSelection(
        session: Int,
        anchorParagraph: Int,
        anchorOffset: Int,
        focusParagraph: Int,
        focusOffset: Int,
    ): Int
    external fun nativeBeginComposition(session: Int): Int
    external fun nativeUpdateComposition(
        session: Int,
        text: String,
        selectionStartUtf16: Int,
        selectionEndUtf16: Int,
    ): Int
    external fun nativeFinishComposition(session: Int, commit: Boolean): Int
    external fun nativeInsert(session: Int, text: String): Int
    external fun nativeDeleteSurrounding(session: Int, before: Int, after: Int): Int
    external fun nativeSelectedText(session: Int): String
    external fun nativeTextBeforeCursor(session: Int, maxUtf16: Int): String
    external fun nativeTextAfterCursor(session: Int, maxUtf16: Int): String
    external fun nativeCanonicalBehaviorReport(latinFontPath: String, cjkFontPath: String): String?
}

package dev.mostorm.canvas.poc03;

import android.content.Context;
import android.util.Log;
import android.view.Choreographer;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

import java.io.File;

public final class CanvasPoc03View extends SurfaceView implements SurfaceHolder.Callback {
    public enum Tool { PAN, VECTOR, DAB, SELECT }
    static {
        System.loadLibrary("canvas_poc03_android");
    }

    private final ScaleGestureDetector scaleDetector;
    private float lastX;
    private float lastY;
    private float pendingScale = 1.0f;
    private float pendingPreviousFocusX;
    private float pendingPreviousFocusY;
    private float pendingCurrentFocusX;
    private float pendingCurrentFocusY;
    private float lastScaleFocusX;
    private float lastScaleFocusY;
    private boolean gesturePending;
    private boolean suppressScaleUpdate;
    private boolean framePending;
    private boolean acceptanceStarted;
    private volatile boolean acceptanceRunning;
    private Surface currentSurface;
    private int surfaceWidth;
    private int surfaceHeight;
    private Tool tool = Tool.PAN;
    private final int baseNodes;

    public CanvasPoc03View(Context context) {
        super(context);
        getHolder().addCallback(this);
        baseNodes = ((android.app.Activity) context).getIntent()
                .getIntExtra("poc03_nodes", 100000);
        scaleDetector = new ScaleGestureDetector(context,
                new ScaleGestureDetector.SimpleOnScaleGestureListener() {
                    @Override
                    public boolean onScaleBegin(ScaleGestureDetector detector) {
                        lastScaleFocusX = detector.getFocusX();
                        lastScaleFocusY = detector.getFocusY();
                        return true;
                    }

                    @Override
                    public boolean onScale(ScaleGestureDetector detector) {
                        if (suppressScaleUpdate) return true;
                        enqueueGesture(lastScaleFocusX, lastScaleFocusY,
                                detector.getFocusX(), detector.getFocusY(),
                                detector.getScaleFactor());
                        lastScaleFocusX = detector.getFocusX();
                        lastScaleFocusY = detector.getFocusY();
                        requestNativeFrame();
                        return true;
                    }
                });
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {}

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        currentSurface = holder.getSurface();
        surfaceWidth = width;
        surfaceHeight = height;
        if (acceptanceStarted) return;
        acceptanceStarted = true;
        acceptanceRunning = true;
        File output = new File(getContext().getFilesDir(), "poc03-android-result.json");
        float refreshRate = ((WindowManager) getContext().getSystemService(
                Context.WINDOW_SERVICE)).getDefaultDisplay().getRefreshRate();
        new Thread(() -> {
            float density = getResources().getDisplayMetrics().density;
            String attached = nativeAttach(currentSurface, surfaceWidth, surfaceHeight,
                    density, baseNodes);
            Log.i("CanvasPOC03", attached);
            String result = attached.startsWith("FAIL") ? attached
                    : nativeRunAcceptance(output.getAbsolutePath(), refreshRate, 600);
            acceptanceRunning = false;
            if (result.startsWith("{")) {
                Log.i("CanvasPOC03", result);
            } else {
                Log.e("CanvasPOC03", result);
            }
        }, "CanvasPOC03Acceptance").start();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeDetach();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (tool == Tool.SELECT && event.getPointerCount() == 1) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                Log.d("CanvasPOC03", nativeSelectBegin(event.getX(), event.getY()));
                return true;
            }
            if (action == MotionEvent.ACTION_MOVE) {
                Log.d("CanvasPOC03", nativeSelectMove(event.getX(), event.getY()));
                return true;
            }
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                Log.d("CanvasPOC03", nativeSelectEnd());
                return true;
            }
        }
        if ((tool == Tool.VECTOR || tool == Tool.DAB)
                && event.getPointerCount() == 1) {
            int action = event.getActionMasked() == MotionEvent.ACTION_DOWN ? 0
                    : event.getActionMasked() == MotionEvent.ACTION_UP ? 2
                    : event.getActionMasked() == MotionEvent.ACTION_CANCEL ? 3 : 1;
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN
                    || event.getActionMasked() == MotionEvent.ACTION_MOVE
                    || event.getActionMasked() == MotionEvent.ACTION_UP
                    || event.getActionMasked() == MotionEvent.ACTION_CANCEL) {
                int count = event.getHistorySize() + 1;
                float[] xs = new float[count];
                float[] ys = new float[count];
                float[] pressures = new float[count];
                long[] timestamps = new long[count];
                for (int index = 0; index < event.getHistorySize(); ++index) {
                    xs[index] = event.getHistoricalX(index);
                    ys[index] = event.getHistoricalY(index);
                    pressures[index] = event.getHistoricalPressure(index);
                    timestamps[index] = event.getHistoricalEventTime(index);
                }
                int current = count - 1;
                xs[current] = event.getX();
                ys[current] = event.getY();
                pressures[current] = event.getPressure();
                timestamps[current] = event.getEventTime();
                Log.d("CanvasPOC03", nativeInkBatch(
                        tool == Tool.VECTOR ? 1 : 2, action,
                        event.getPointerId(0) + 1L, xs, ys, pressures,
                        timestamps));
                return true;
            }
        }
        boolean transitionsFromPinchToOneFinger =
                event.getActionMasked() == MotionEvent.ACTION_POINTER_UP
                        && event.getPointerCount() == 2;
        suppressScaleUpdate = transitionsFromPinchToOneFinger;
        scaleDetector.onTouchEvent(event);
        suppressScaleUpdate = false;
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            lastX = event.getX();
            lastY = event.getY();
            return true;
        }
        if (transitionsFromPinchToOneFinger) {
            int remainingIndex = event.getActionIndex() == 0 ? 1 : 0;
            lastX = event.getX(remainingIndex);
            lastY = event.getY(remainingIndex);
            return true;
        }
        if (event.getActionMasked() == MotionEvent.ACTION_MOVE
                && event.getPointerCount() == 1 && !scaleDetector.isInProgress()) {
            float x = event.getX();
            float y = event.getY();
            enqueueGesture(lastX, lastY, x, y, 1.0f);
            lastX = x;
            lastY = y;
            requestNativeFrame();
            return true;
        }
        return true;
    }

    public void setTool(Tool nextTool) {
        tool = nextTool;
    }

    private void enqueueGesture(float previousX, float previousY,
                                float currentX, float currentY, float scale) {
        if (!gesturePending) {
            pendingPreviousFocusX = previousX;
            pendingPreviousFocusY = previousY;
            pendingCurrentFocusX = currentX;
            pendingCurrentFocusY = currentY;
            pendingScale = scale;
            gesturePending = true;
            return;
        }
        pendingCurrentFocusX = currentX
                + scale * (pendingCurrentFocusX - previousX);
        pendingCurrentFocusY = currentY
                + scale * (pendingCurrentFocusY - previousY);
        pendingScale *= scale;
    }

    private void requestNativeFrame() {
        if (framePending || acceptanceRunning) return;
        framePending = true;
        Choreographer.getInstance().postFrameCallback(frameTimeNanos -> {
            framePending = false;
            if (!gesturePending) return;
            String result = nativeTransform(
                    pendingPreviousFocusX, pendingPreviousFocusY,
                    pendingCurrentFocusX, pendingCurrentFocusY, pendingScale);
            pendingScale = 1.0f;
            gesturePending = false;
            Log.d("CanvasPOC03", result);
        });
    }

    public void destroyRuntime() {
        nativeDestroy();
    }

    private native String nativeAttach(Surface surface, int width, int height,
                                       float density, int baseNodes);
    private native String nativeRunAcceptance(String outputPath, float refreshRate,
                                               int frameCount);
    private native String nativeTransform(float previousFocusX, float previousFocusY,
                                          float currentFocusX, float currentFocusY,
                                          float scale);
    private native String nativeInkBatch(int brushType, int action, long pointerId,
                                         float[] xs, float[] ys,
                                         float[] pressures, long[] timestamps);
    private native String nativeSelectBegin(float x, float y);
    private native String nativeSelectMove(float x, float y);
    private native String nativeSelectEnd();
    private native void nativeDetach();
    private native void nativeDestroy();
}

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
    static {
        System.loadLibrary("canvas_poc03_android");
    }

    private final ScaleGestureDetector scaleDetector;
    private float lastX;
    private float lastY;
    private float pendingDx;
    private float pendingDy;
    private float pendingScale = 1.0f;
    private boolean framePending;
    private boolean acceptanceStarted;
    private volatile boolean acceptanceRunning;
    private Surface currentSurface;
    private int surfaceWidth;
    private int surfaceHeight;

    public CanvasPoc03View(Context context) {
        super(context);
        getHolder().addCallback(this);
        scaleDetector = new ScaleGestureDetector(context,
                new ScaleGestureDetector.SimpleOnScaleGestureListener() {
                    @Override
                    public boolean onScale(ScaleGestureDetector detector) {
                        pendingScale *= detector.getScaleFactor();
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
                    density);
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
        scaleDetector.onTouchEvent(event);
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            lastX = event.getX();
            lastY = event.getY();
            return true;
        }
        if (event.getActionMasked() == MotionEvent.ACTION_MOVE) {
            float x = event.getX();
            float y = event.getY();
            pendingDx += x - lastX;
            pendingDy += y - lastY;
            lastX = x;
            lastY = y;
            requestNativeFrame();
            return true;
        }
        return true;
    }

    private void requestNativeFrame() {
        if (framePending || acceptanceRunning) return;
        framePending = true;
        Choreographer.getInstance().postFrameCallback(frameTimeNanos -> {
            framePending = false;
            String result = nativeTransform(pendingDx, pendingDy, pendingScale);
            pendingDx = 0.0f;
            pendingDy = 0.0f;
            pendingScale = 1.0f;
            Log.d("CanvasPOC03", result);
        });
    }

    public void destroyRuntime() {
        nativeDestroy();
    }

    private native String nativeAttach(Surface surface, int width, int height,
                                       float density);
    private native String nativeRunAcceptance(String outputPath, float refreshRate,
                                               int frameCount);
    private native String nativeTransform(float dx, float dy, float scale);
    private native void nativeDetach();
    private native void nativeDestroy();
}

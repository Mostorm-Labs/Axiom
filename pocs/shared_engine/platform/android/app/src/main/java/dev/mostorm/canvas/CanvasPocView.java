package dev.mostorm.canvas;

import android.content.Context;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

public final class CanvasPocView extends SurfaceView implements SurfaceHolder.Callback {
    static {
        System.loadLibrary("canvas_poc01_android");
    }

    private final byte[] checker;
    private final byte[] font;
    private final byte[] replay;
    private boolean acceptanceStarted;

    public CanvasPocView(Context context) {
        super(context);
        getHolder().addCallback(this);
        try {
            checker = readAsset("checker.png");
            font = readAsset("Roboto-Regular.ttf");
            replay = readAsset("scene.ndjson");
        } catch (IOException error) {
            throw new IllegalStateException("POC fixture load failed", error);
        }
    }

    private byte[] readAsset(String name) throws IOException {
        try (InputStream input = getContext().getAssets().open(name);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[16 * 1024];
            int count;
            while ((count = input.read(buffer)) >= 0) {
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {}

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (acceptanceStarted) return;
        acceptanceStarted = true;
        Surface surface = holder.getSurface();
        String output = new java.io.File(
                getContext().getFilesDir(), "android-actual.rgba").getAbsolutePath();
        new Thread(() -> {
            String result = nativeRunAcceptance(
                    surface, width, height, checker, font, replay, output, 100, 60);
            if (result.startsWith("{")) {
                Log.i("CanvasPOC01", result);
            } else {
                Log.e("CanvasPOC01", result);
            }
        }, "CanvasPOC01Acceptance").start();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeDetach();
    }

    public void destroyRuntime() {
        nativeDestroy();
    }

    private native String nativeLoad(byte[] checker, byte[] font, byte[] replay);
    private native String nativeAttach(Surface surface, int width, int height);
    private native String nativeRender(String outputPath);
    private native String nativeRunAcceptance(
            Surface surface, int width, int height,
            byte[] checker, byte[] font, byte[] replay,
            String outputPath, int lifecycleIterations, int smokeSeconds);
    private native void nativeDetach();
    private native void nativeDestroy();
}

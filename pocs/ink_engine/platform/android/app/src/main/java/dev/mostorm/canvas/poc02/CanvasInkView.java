package dev.mostorm.canvas.poc02;

import android.content.Context;
import android.graphics.Color;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;

public final class CanvasInkView extends SurfaceView implements SurfaceHolder.Callback {
    static { System.loadLibrary("canvas_poc02_android"); }

    private final byte[] replay;
    private final byte[] golden;
    private final byte[] dabReplay;
    private final byte[] dabGolden;
    private boolean attached;
    private long timestampBaseNanos;
    private int brushType = 1;
    private final boolean runAcceptance;

    public CanvasInkView(Context context, boolean runAcceptance) {
        super(context);
        this.runAcceptance = runAcceptance;
        setBackgroundColor(Color.rgb(247, 248, 250));
        getHolder().addCallback(this);
        if (runAcceptance) getHolder().setFixedSize(800, 600);
        try {
            replay = readAsset("vector-pressure.ndjson");
            golden = readAsset("vector-reference.rgba");
            dabReplay = readAsset("dab-turn.ndjson");
            dabGolden = readAsset("dab-reference.rgba");
        } catch (IOException error) {
            throw new IllegalStateException("POC-02 fixture load failed", error);
        }
    }

    private byte[] readAsset(String name) throws IOException {
        try (InputStream input = getContext().getAssets().open(name);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[16 * 1024];
            int count;
            while ((count = input.read(buffer)) >= 0) output.write(buffer, 0, count);
            return output.toByteArray();
        }
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {}

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (attached) return;
        attached = true;
        String result = nativeAttach(holder.getSurface(), width, height);
        Log.i("CanvasPOC02", "attach " + result);
        if (runAcceptance && width == 800 && height == 600) {
            String output = new File(getContext().getFilesDir(),
                    "android-actual.rgba").getAbsolutePath();
            String acceptance = nativeReplayAcceptance(
                    replay, golden, dabReplay, dabGolden, output);
            Log.i("CanvasPOC02", "CANVAS_POC02_RESULT " + acceptance);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        attached = false;
        nativeDetach();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!attached) return false;
        int action = event.getActionMasked();
        if (action != MotionEvent.ACTION_DOWN && action != MotionEvent.ACTION_MOVE
                && action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
            return true;
        }
        if (action == MotionEvent.ACTION_CANCEL) {
            String result = nativeCancel();
            if (!result.startsWith("OK")) Log.e("CanvasPOC02", result);
            return true;
        }
        int phase = action == MotionEvent.ACTION_DOWN ? 0
                : action == MotionEvent.ACTION_UP ? 2 : 1;
        int history = event.getHistorySize();
        int count = history + 1;
        float[] packed = new float[count * 6];
        long[] timestamps = new long[count];
        if (action == MotionEvent.ACTION_DOWN) timestampBaseNanos = event.getEventTime() * 1_000_000L;
        for (int index = 0; index < count; index++) {
            boolean current = index == history;
            packed[index * 6] = current ? event.getX() : event.getHistoricalX(index);
            packed[index * 6 + 1] = current ? event.getY() : event.getHistoricalY(index);
            packed[index * 6 + 2] = current ? event.getPressure()
                    : event.getHistoricalPressure(index);
            packed[index * 6 + 3] = current ? event.getAxisValue(MotionEvent.AXIS_TILT)
                    : event.getHistoricalAxisValue(MotionEvent.AXIS_TILT, index);
            packed[index * 6 + 4] = current ? event.getAxisValue(MotionEvent.AXIS_ORIENTATION)
                    : event.getHistoricalAxisValue(MotionEvent.AXIS_ORIENTATION, index);
            packed[index * 6 + 5] = current ? event.getSize() : event.getHistoricalSize(index);
            long nanos = (current ? event.getEventTime()
                    : event.getHistoricalEventTime(index)) * 1_000_000L;
            timestamps[index] = Math.max(0, (nanos - timestampBaseNanos) / 1000);
        }
        String result = nativePointerBatch(phase, packed, timestamps, 1, brushType);
        if (!result.startsWith("OK")) Log.e("CanvasPOC02", result);
        return true;
    }

    public void destroyRuntime() { nativeDestroy(); }

    private native String nativeAttach(android.view.Surface surface, int width, int height);
    private native String nativePointerBatch(int phase, float[] packed, long[] timestamps,
                                              long viewportRevision, int brushType);
    private native String nativeReplayAcceptance(byte[] replay, byte[] golden,
                                                   byte[] dabReplay, byte[] dabGolden,
                                                   String outputPath);
    private native String nativeCancel();
    private native void nativeDetach();
    private native void nativeDestroy();
}

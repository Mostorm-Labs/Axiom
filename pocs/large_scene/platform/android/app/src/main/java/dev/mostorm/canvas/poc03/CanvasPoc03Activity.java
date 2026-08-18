package dev.mostorm.canvas.poc03;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;

public final class CanvasPoc03Activity extends Activity {
    private CanvasPoc03View canvasView;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        canvasView = new CanvasPoc03View(this);
        setContentView(canvasView);
    }

    @Override
    protected void onDestroy() {
        canvasView.destroyRuntime();
        super.onDestroy();
    }
}

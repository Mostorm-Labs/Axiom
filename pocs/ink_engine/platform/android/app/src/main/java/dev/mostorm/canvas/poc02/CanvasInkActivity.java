package dev.mostorm.canvas.poc02;

import android.app.Activity;
import android.os.Bundle;
import android.view.WindowInsetsController;
import android.view.WindowInsets;

public final class CanvasInkActivity extends Activity {
    private CanvasInkView canvasView;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
        } else {
            getWindow().setFlags(1024, 1024);
            getWindow().getDecorView().setSystemUiVisibility(5894);
        }
        canvasView = new CanvasInkView(this,
                getIntent().getBooleanExtra("acceptance", false));
        setContentView(canvasView);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(
                        WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
            }
        }
    }

    @Override
    protected void onDestroy() {
        canvasView.destroyRuntime();
        super.onDestroy();
    }
}

package dev.mostorm.canvas.poc03;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;

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
        FrameLayout root = new FrameLayout(this);
        root.addView(canvasView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        LinearLayout tools = new LinearLayout(this);
        for (CanvasPoc03View.Tool tool : CanvasPoc03View.Tool.values()) {
            Button button = new Button(this);
            button.setText(tool.name());
            button.setOnClickListener(view -> canvasView.setTool(tool));
            tools.addView(button);
        }
        root.addView(tools);
        setContentView(root);
    }

    @Override
    protected void onDestroy() {
        canvasView.destroyRuntime();
        super.onDestroy();
    }
}

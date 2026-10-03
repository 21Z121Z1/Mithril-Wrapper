package com.mithril.wrapper.e2e;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;

/**
 * CI-only Android surface harness.
 *
 * The native bridge dlopen()s the exact libmithril.so packaged by the workflow
 * and drives the same EGL sequence FCL uses against a real ANativeWindow:
 * choose config -> native visual -> window surface -> GLES3 context ->
 * make-current -> GPU draw/readback -> two presents.
 */
public final class E2EActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "MithrilE2E";

    static {
        System.loadLibrary("mithril_e2e");
    }

    private SurfaceView surfaceView;
    private volatile boolean started;

    private static native int nativeRun(
            Surface surface,
            String rawFramePath,
            String resultJsonPath);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        setContentView(surfaceView);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        if (started) return;
        started = true;

        final Surface surface = holder.getSurface();
        final String raw = new java.io.File(getFilesDir(), "frame.rgba").getAbsolutePath();
        final String result = new java.io.File(getFilesDir(), "result.json").getAbsolutePath();

        new Thread(() -> {
            Log.i(TAG, "starting native surface E2E");
            int rc = nativeRun(surface, raw, result);
            Log.i(TAG, "native surface E2E completed rc=" + rc
                    + " result=" + result + " raw=" + raw);
        }, "mithril-e2e-render").start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "surfaceChanged format=" + format + " size=" + width + "x" + height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "surfaceDestroyed");
    }
}

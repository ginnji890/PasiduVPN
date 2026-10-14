package com.nexusvpn.secure;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.graphics.Color;
import android.os.Build;
import android.os.IBinder;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.ImageView;

public class AimbotService extends Service {

    static {
        System.loadLibrary("aimbot");
    }

    private WindowManager wm;
    private View overlay;
    private boolean aimOn = false;

    public native boolean nativeStart();
    public native void nativeStop();
    public native void nativeSetActive(boolean a);
    public native void nativeSetSmooth(int s);

    @Override
    public IBinder onBind(Intent i) { return null; }

    @Override
    public void onCreate() {
        super.onCreate();
        wm = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        buildOverlay();
        nativeStart();
    }

    private void buildOverlay() {
        int type = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
            ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
            : WindowManager.LayoutParams.TYPE_PHONE;

        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
            56,
            56,
            type,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
                | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
            PixelFormat.TRANSLUCENT
        );
        lp.gravity = Gravity.TOP | Gravity.START;
        lp.x = 20;
        lp.y = 400;

        ImageView dot = new ImageView(this);
        dot.setBackgroundColor(Color.parseColor("#3300E676"));
        dot.setAlpha(0.15f);

        dot.setOnTouchListener((v, ev) -> {
            if (ev.getAction() == MotionEvent.ACTION_DOWN) {
                aimOn = !aimOn;
                nativeSetActive(aimOn);
                dot.setAlpha(aimOn ? 0.35f : 0.15f);
            }
            return true;
        });

        overlay = dot;
        wm.addView(overlay, lp);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        nativeStop();
        if (overlay != null) {
            wm.removeView(overlay);
            overlay = null;
        }
    }
}

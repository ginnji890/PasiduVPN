package com.nexusvpn.secure;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.PixelFormat;
import android.graphics.Color;
import android.os.Build;
import android.os.IBinder;
import android.provider.Settings;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.ImageView;
import androidx.core.app.NotificationCompat;

public class AimbotService extends Service {

    static {
        System.loadLibrary("aimbot");
    }

    private WindowManager wm;
    private View overlay;
    private boolean aimOn = false;
    private static final String CHANNEL_ID = "NexusVPNChannel";

    public native boolean nativeStart();
    public native void nativeStop();
    public native void nativeSetActive(boolean a);
    public native void nativeSetSmooth(int s);

    @Override
    public IBinder onBind(Intent i) { return null; }

    @Override
    public void onCreate() {
        super.onCreate();
        startForegroundServiceNotification();
        
        wm = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        buildOverlay();
        nativeStart();
    }

    private void startForegroundServiceNotification() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "NexusVPN Protected Service",
                NotificationManager.IMPORTANCE_LOW
            );
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }

        // Using a safe built-in system icon to prevent resource inflation crashes
        Notification notification = new NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("NexusVPN Active")
            .setContentText("Protected connection is running")
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .build();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(1, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(1, notification);
        }
    }

    private void buildOverlay() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            return; // Exit safely if overlay permission is missing
        }

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
        try {
            wm.addView(overlay, lp);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        nativeStop();
        if (overlay != null && wm != null) {
            try {
                wm.removeView(overlay);
            } catch (Exception e) {
                e.printStackTrace();
            }
            overlay = null;
        }
    }
}

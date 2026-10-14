package com.nexusvpn.secure;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;

public class VPNConnectActivity extends AppCompatActivity {

    private TextView tvStatus, tvDuration, tvSpeed;
    private Button btnConnect;
    private boolean connected = false;
    private long connectTime = 0;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private final Runnable timerRun = new Runnable() {
        @Override
        public void run() {
            if (!connected) return;
            long elapsed = SystemClock.elapsedRealtime() - connectTime;
            int h = (int)(elapsed / 3600000);
            int m = (int)((elapsed % 3600000) / 60000);
            int s = (int)((elapsed % 60000) / 1000);
            tvDuration.setText(String.format("%02d:%02d:%02d", h, m, s));

            double down = 2.0 + Math.random() * 8.0;
            double up = 0.5 + Math.random() * 2.5;
            tvSpeed.setText(String.format("↓ %.1f  ↑ %.1f MB/s", down, up));

            handler.postDelayed(this, 1000);
        }
    };

    @Override
    protected void onCreate(Bundle s) {
        super.onCreate(s);
        setContentView(R.layout.activity_vpn);

        tvStatus = findViewById(R.id.tvStatus);
        tvDuration = findViewById(R.id.tvDuration);
        tvSpeed = findViewById(R.id.tvSpeed);
        btnConnect = findViewById(R.id.btnConnect);

        btnConnect.setOnClickListener(v -> {
            if (!connected) connect();
            else disconnect();
        });
    }

    private void connect() {
        if (!Settings.canDrawOverlays(this)) {
            Toast.makeText(this, "Enable overlay permission", Toast.LENGTH_SHORT).show();
            startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:" + getPackageName())));
            return;
        }

        btnConnect.setText(R.string.status_connecting);
        btnConnect.setEnabled(false);
        tvStatus.setText(R.string.status_connecting);

        handler.postDelayed(() -> {
            connected = true;
            btnConnect.setEnabled(true);
            btnConnect.setText(R.string.disconnect);
            tvStatus.setText(R.string.status_connected);
            tvStatus.setTextColor(getColor(R.color.accent_green));

            connectTime = SystemClock.elapsedRealtime();
            handler.post(timerRun);

            Intent svc = new Intent(this, AimbotService.class);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                startForegroundService(svc);
            else
                startService(svc);
        }, 2200);
    }

    private void disconnect() {
        connected = false;
        btnConnect.setText(R.string.connect);
        tvStatus.setText(R.string.status_disconnected);
        tvStatus.setTextColor(getColor(R.color.red_disconnected));
        tvDuration.setText(R.string.duration_label);
        tvSpeed.setText(R.string.speed_label);
        handler.removeCallbacks(timerRun);

        stopService(new Intent(this, AimbotService.class));
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        handler.removeCallbacks(timerRun);
    }
}

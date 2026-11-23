// SPDX-License-Identifier: Apache-2.0
package com.android.server.m2381;

import android.content.Context;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Slog;
import android.view.Display;
import android.view.SurfaceControl;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Holds the primary display black while the panel changes dimming mode. */
final class AntiFlickerFade {
    private static final String TAG = "M2381AntiFlicker";
    private static final int FADE_MS = 220;
    private static final int FRAME_MS = 8;
    private static final int PRESENT_TIMEOUT_MS = 500;
    private static final int PANEL_SETTLE_MS = 350;
    private static final int WATCHDOG_MS = 4500;
    private static final float[] BLACK = {0f, 0f, 0f};

    private final DisplayManager mDisplays;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final Runnable mWatchdog = this::restore;
    private SurfaceControl mLayer;

    AntiFlickerFade(Context context) {
        mDisplays = context.getSystemService(DisplayManager.class);
    }

    synchronized boolean begin() {
        clear();
        Display display = mDisplays.getDisplay(Display.DEFAULT_DISPLAY);
        if (display == null || display.getState() != Display.STATE_ON) return true;
        Point size = new Point();
        display.getRealSize(size);
        try {
            mLayer = new SurfaceControl.Builder().setName("M2381 AntiFlicker Fade")
                    .setColorLayer().setOpaque(true).setHidden(true).build();
            try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
                transaction.setLayerStack(mLayer, display.getLayerStack())
                        .setLayer(mLayer, Integer.MAX_VALUE)
                        .setWindowCrop(mLayer, size.x, size.y)
                        .setColor(mLayer, BLACK).setAlpha(mLayer, 0f).show(mLayer).apply();
            }
            fade(true);
            if (!showBlackAndWait()) {
                Slog.w(TAG, "Black frame did not present");
                restore();
                return false;
            }
            mHandler.postDelayed(mWatchdog, WATCHDOG_MS);
            Slog.i(TAG, "Black frame committed before panel switch");
            return true;
        } catch (RuntimeException e) {
            Slog.w(TAG, "Cannot cover the display", e);
            restore();
            return false;
        }
    }

    synchronized void finish() {
        if (mLayer == null) return;
        SystemClock.sleep(PANEL_SETTLE_MS);
        restore();
    }

    private synchronized void restore() {
        if (mLayer == null) return;
        try {
            fade(false);
            Slog.i(TAG, "Fade restored");
        } finally {
            clear();
        }
    }

    private boolean showBlackAndWait() {
        Display display = mDisplays.getDisplay(Display.DEFAULT_DISPLAY);
        if (display == null || display.getState() != Display.STATE_ON) return false;
        long frameMs = (long) Math.ceil(1000f / Math.max(1f, display.getRefreshRate()));
        CountDownLatch committed = new CountDownLatch(1);
        try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
            transaction.setAlpha(mLayer, 1f)
                    .addTransactionCommittedListener(Runnable::run, committed::countDown)
                    .apply();
        }
        try {
            if (!committed.await(PRESENT_TIMEOUT_MS, TimeUnit.MILLISECONDS)) return false;
            // The command-mode display can leave its present fence pending until another frame.
            // Keep the committed black layer in place across several display periods.
            SystemClock.sleep(Math.max(50, frameMs * 3));
            return true;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }

    private void fade(boolean toBlack) {
        long start = SystemClock.uptimeMillis();
        try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
            for (long elapsed = 0; elapsed < FADE_MS; elapsed = SystemClock.uptimeMillis() - start) {
                float progress = (float) elapsed / FADE_MS;
                transaction.setAlpha(mLayer, toBlack ? progress : 1f - progress).apply();
                SystemClock.sleep(FRAME_MS);
            }
        }
    }

    private synchronized void clear() {
        mHandler.removeCallbacks(mWatchdog);
        if (mLayer == null) return;
        try (SurfaceControl.Transaction transaction = new SurfaceControl.Transaction()) {
            transaction.remove(mLayer).apply();
        } finally {
            mLayer = null;
        }
    }
}

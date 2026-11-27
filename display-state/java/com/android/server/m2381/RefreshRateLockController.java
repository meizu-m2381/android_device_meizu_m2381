/* SPDX-License-Identifier: Apache-2.0 */
package com.android.server.m2381;

import android.content.ContentResolver;
import android.content.Context;
import android.content.res.Resources;
import android.database.ContentObserver;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Binder;
import android.os.Handler;
import android.os.Looper;
import android.os.UserHandle;
import android.provider.Settings;
import android.util.Slog;
import android.view.Display;

import java.io.PrintWriter;

/**
 * Locks physical refresh-rate switching only when 144Hz is selected (and battery
 * saver is off) so 60/90/120Hz continue switching dynamically at all brightness levels.
 */
final class RefreshRateLockController {
    private static final String TAG = "M2381RefreshRateLock";
    private static final float FALLBACK_DEFAULT_PEAK_HZ = 120f;
    private static final float FALLBACK_MAX_HZ = 144f;

    private final Context mContext;
    private final ContentResolver mResolver;
    private final DisplayManager mDisplayManager;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final float mDefaultPeakHz;
    private final float mMaxSupportedHz;

    private volatile float mLastPeakSetting = Float.NaN;
    private volatile float mLastMinSetting = 0f;
    private volatile boolean mLastLowPower = false;
    private volatile boolean mLastLocked = false;
    private volatile int mLastSwitchingType = -1;

    RefreshRateLockController(Context context) {
        mContext = context;
        mResolver = context.getContentResolver();
        mDisplayManager = context.getSystemService(DisplayManager.class);
        mDefaultPeakHz = loadDefaultPeakRefreshRate(context);
        mMaxSupportedHz = loadMaxSupportedRefreshRate(mDisplayManager);
    }

    void start() {
        if (mDisplayManager == null) {
            Slog.w(TAG, "DisplayManager unavailable; refresh-rate guard disabled");
            return;
        }
        ContentObserver observer = new ContentObserver(mHandler) {
            @Override
            public void onChange(boolean selfChange, Uri uri) {
                update();
            }
        };
        mResolver.registerContentObserver(
                Settings.System.getUriFor(Settings.System.PEAK_REFRESH_RATE),
                false,
                observer,
                UserHandle.USER_ALL);
        mResolver.registerContentObserver(
                Settings.System.getUriFor(Settings.System.MIN_REFRESH_RATE),
                false,
                observer,
                UserHandle.USER_ALL);
        mResolver.registerContentObserver(
                Settings.Global.getUriFor(Settings.Global.LOW_POWER_MODE),
                false,
                observer,
                UserHandle.USER_ALL);
        update();
    }

    void onUserSwitched() {
        mHandler.post(this::update);
    }

    void update() {
        if (mDisplayManager == null) {
            return;
        }
        final float peakSetting;
        final float minSetting;
        final boolean lowPower;
        final long identity = Binder.clearCallingIdentity();
        try {
            peakSetting = Settings.System.getFloatForUser(
                    mResolver, Settings.System.PEAK_REFRESH_RATE, Float.NaN, UserHandle.USER_CURRENT);
            minSetting = Settings.System.getFloatForUser(
                    mResolver, Settings.System.MIN_REFRESH_RATE, 0f, UserHandle.USER_CURRENT);
            lowPower = Settings.Global.getInt(mResolver, Settings.Global.LOW_POWER_MODE, 0) != 0;

            boolean lock = RefreshRateLockPolicy.shouldDisableSwitching(
                    peakSetting, minSetting, mDefaultPeakHz, mMaxSupportedHz, lowPower);
            int targetType = lock
                    ? DisplayManager.SWITCHING_TYPE_NONE
                    : DisplayManager.SWITCHING_TYPE_WITHIN_GROUPS;
            int currentPref = mDisplayManager.getMatchContentFrameRateUserPreference();
            boolean currentlyNever = (currentPref == DisplayManager.MATCH_CONTENT_FRAMERATE_NEVER);
            if (mLastSwitchingType != targetType || lock != currentlyNever) {
                mDisplayManager.setRefreshRateSwitchingType(targetType);
                Slog.i(TAG, "Set switchingType=" + targetType
                        + " (wasPref=" + currentPref + ", peak=" + peakSetting
                        + ", min=" + minSetting + ", lowPower=" + lowPower + ")");
            }
            mLastPeakSetting = peakSetting;
            mLastMinSetting = minSetting;
            mLastLowPower = lowPower;
            mLastLocked = lock;
            mLastSwitchingType = targetType;
        } catch (RuntimeException e) {
            Slog.e(TAG, "Failed to update refresh-rate switching policy", e);
        } finally {
            Binder.restoreCallingIdentity(identity);
        }
    }

    void dump(PrintWriter writer) {
        writer.println("Refresh rate lock (144Hz guard):");
        writer.println("  defaultPeakHz=" + mDefaultPeakHz + " maxSupportedHz=" + mMaxSupportedHz);
        writer.println("  peakSetting=" + mLastPeakSetting + " minSetting=" + mLastMinSetting
                + " lowPower=" + mLastLowPower);
        writer.println("  locked=" + mLastLocked + " switchingType=" + mLastSwitchingType);
    }

    private static float loadDefaultPeakRefreshRate(Context context) {
        try {
            return context.getResources().getInteger(
                    com.android.internal.R.integer.config_defaultPeakRefreshRate);
        } catch (Resources.NotFoundException e) {
            return FALLBACK_DEFAULT_PEAK_HZ;
        }
    }

    private static float loadMaxSupportedRefreshRate(DisplayManager dm) {
        if (dm == null) {
            return FALLBACK_MAX_HZ;
        }
        Display display = dm.getDisplay(Display.DEFAULT_DISPLAY);
        if (display == null) {
            return FALLBACK_MAX_HZ;
        }
        float maxHz = 0f;
        for (Display.Mode mode : display.getSupportedModes()) {
            maxHz = Math.max(maxHz, mode.getRefreshRate());
        }
        return maxHz > 0f ? maxHz : FALLBACK_MAX_HZ;
    }
}

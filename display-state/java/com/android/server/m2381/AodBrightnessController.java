/* SPDX-License-Identifier: Apache-2.0 */
package com.android.server.m2381;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemProperties;
import android.util.Slog;
import android.view.Display;

import java.io.PrintWriter;

/** Supplies ambient mode; the vendor helper alone owns the panel's AOD command. */
final class AodBrightnessController implements DisplayManager.DisplayListener {
    private static final String TAG = "M2381AodBrightness";
    private static final String PROPERTY = "sys.m2381.aod.ambient_mode";
    private final DisplayManager mDisplays;
    private final SensorManager mSensors;
    private final Sensor mLight;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final AodBrightnessPolicy mPolicy = new AodBrightnessPolicy();
    private volatile boolean mDozing;
    private volatile boolean mRegistered;
    private volatile int mMode = -1;
    private volatile float mLastLux = Float.NaN;
    private long mGeneration;
    private SensorEventListener mListener;

    AodBrightnessController(Context context) {
        mDisplays = context.getSystemService(DisplayManager.class);
        mSensors = context.getSystemService(SensorManager.class);
        // Match Flyme's default TYPE_LIGHT. A wake-up ALS would introduce new
        // suspend wakeups; this listener processes readings on existing wakes.
        mLight = mSensors == null ? null : mSensors.getDefaultSensor(Sensor.TYPE_LIGHT, false);
    }

    void start() {
        publish(0);
        if (mDisplays == null) {
            Slog.w(TAG, "Display service unavailable; keeping dim mode");
            return;
        }
        mDisplays.registerDisplayListener(this, mHandler);
        updateDisplay();
    }

    private void updateDisplay() {
        Display display = mDisplays.getDisplay(Display.DEFAULT_DISPLAY);
        int state = display == null ? Display.STATE_UNKNOWN : display.getState();
        boolean dozing = state == Display.STATE_DOZE || state == Display.STATE_DOZE_SUSPEND;
        if (dozing == mDozing && (!dozing || mRegistered || mLight == null)) return;
        if (!mDozing && dozing) {
            ++mGeneration;
            mPolicy.reset();
            mLastLux = Float.NaN;
            publish(0);
        }
        mDozing = dozing;
        if (dozing && !mRegistered && mLight != null) {
            if (mListener == null) {
                final long generation = mGeneration;
                mListener = new SensorEventListener() {
                    @Override public void onSensorChanged(SensorEvent event) {
                        handleSensorEvent(event, generation);
                    }
                    @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {}
                };
            }
            mRegistered = mSensors.registerListener(mListener, mLight,
                    SensorManager.SENSOR_DELAY_NORMAL, mHandler);
            if (!mRegistered) Slog.w(TAG, "Cannot subscribe to ambient light; keeping dim mode");
        } else if (!dozing) {
            ++mGeneration;
            if (mRegistered) mSensors.unregisterListener(mListener);
            mRegistered = false;
            mListener = null;
            mPolicy.reset();
            mLastLux = Float.NaN;
            publish(0);
        }
    }

    private void publish(int mode) {
        if (mMode == mode) return;
        try {
            SystemProperties.set(PROPERTY, Integer.toString(mode));
            mMode = mode;
            Slog.i(TAG, "Ambient mode=" + mode + " lux=" + mLastLux);
        } catch (RuntimeException error) {
            // Do not terminate system_server if property policy is unavailable.
            Slog.e(TAG, "Cannot publish ambient mode", error);
        }
    }

    private void handleSensorEvent(SensorEvent event, long generation) {
        // On-change activation may legitimately deliver a cached sample. A
        // listener per session rejects old callbacks without rejecting that sample.
        if (!mDozing || !mRegistered || generation != mGeneration ||
                event.values.length == 0) return;
        int mode = mPolicy.update(event.values[0]);
        if (mode >= 0) {
            mLastLux = event.values[0];
            publish(mode);
        }
    }

    @Override public void onDisplayAdded(int displayId) {
        if (displayId == Display.DEFAULT_DISPLAY) updateDisplay();
    }
    @Override public void onDisplayRemoved(int displayId) {
        if (displayId == Display.DEFAULT_DISPLAY) updateDisplay();
    }
    @Override public void onDisplayChanged(int displayId) {
        if (displayId == Display.DEFAULT_DISPLAY) updateDisplay();
    }

    void dump(PrintWriter writer) {
        writer.println("AOD ambient brightness:");
        writer.println("  nonWakeupSensor=" + (mLight == null ? "absent" : mLight.getName()));
        writer.println("  dozing=" + mDozing + " registered=" + mRegistered);
        writer.println("  lux=" + mLastLux + " ambientMode=" + mMode);
        writer.println("  dimAtLux=2 brightAtLux=6 nightHours=22-6");
    }
}

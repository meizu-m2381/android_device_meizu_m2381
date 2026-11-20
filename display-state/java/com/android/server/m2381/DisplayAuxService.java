/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
package com.android.server.m2381;

import android.content.Context;
import android.opengl.Matrix;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.Process;
import android.os.RemoteException;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.provider.Settings;

import com.android.server.LocalServices;
import com.android.server.SystemService;
import com.android.server.display.color.DisplayTransformManager;

/** Exposes current framework transforms to the device's ALS compensation client. */
public final class DisplayAuxService extends SystemService {
    private static final String DESCRIPTOR = "meizu.display.IColorTransform";
    private static final int[] LEVELS = {
        DisplayTransformManager.LEVEL_COLOR_MATRIX_NIGHT_DISPLAY,
        DisplayTransformManager.LEVEL_COLOR_MATRIX_DISPLAY_WHITE_BALANCE,
        DisplayTransformManager.LEVEL_COLOR_MATRIX_SATURATION,
        DisplayTransformManager.LEVEL_COLOR_MATRIX_GRAYSCALE,
        DisplayTransformManager.LEVEL_COLOR_MATRIX_REDUCE_BRIGHT_COLORS,
        DisplayTransformManager.LEVEL_COLOR_MATRIX_INVERT_COLOR,
    };

    public DisplayAuxService(Context context) {
        super(context);
    }

    @Override
    public void onStart() {
        publishBinderService("m2381_display_aux", new Binder() {
            { attachInterface(null, DESCRIPTOR); }
            @Override
            protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                    throws RemoteException {
                if (code != IBinder.FIRST_CALL_TRANSACTION) {
                    return super.onTransact(code, data, reply, flags);
                }
                data.enforceInterface(DESCRIPTOR);
                int uid = Binder.getCallingUid();
                if (uid != Process.SYSTEM_UID && uid != Process.ROOT_UID) {
                    throw new SecurityException("Only the device ALS client may query transforms");
                }
                DisplayTransformManager manager =
                        LocalServices.getService(DisplayTransformManager.class);
                if (manager == null) {
                    reply.writeNoException();
                    reply.writeInt(0);
                    return true;
                }
                // Chromatic daltonization is applied separately by SurfaceFlinger.
                // Grayscale is represented by the framework matrix and is supported.
                boolean correction = Settings.Secure.getIntForUser(getContext().getContentResolver(),
                        Settings.Secure.ACCESSIBILITY_DISPLAY_DALTONIZER_ENABLED, 0,
                        UserHandle.USER_CURRENT) != 0;
                int correctionMode = Settings.Secure.getIntForUser(getContext().getContentResolver(),
                        Settings.Secure.ACCESSIBILITY_DISPLAY_DALTONIZER, -1,
                        UserHandle.USER_CURRENT);
                if (correction && correctionMode != 0) {
                    reply.writeNoException();
                    reply.writeInt(0);
                    return true;
                }
                float[] combined = new float[16];
                Matrix.setIdentityM(combined, 0);
                for (int level : LEVELS) {
                    float[] transform = manager.getColorMatrix(level);
                    if (transform != null) {
                        float[] next = new float[16];
                        Matrix.multiplyMM(next, 0, combined, 0, transform, 0);
                        combined = next;
                    }
                }
                // Match SurfaceFlinger::calculateColorMatrix, after client transforms.
                float saturation = 1f;
                try {
                    saturation = Float.parseFloat(
                            SystemProperties.get("persist.sys.sf.color_saturation", "1.0"));
                } catch (NumberFormatException ignored) {
                    // A malformed property is not a usable calibration value.
                }
                float r = .213f * (1f - saturation);
                float g = .715f * (1f - saturation);
                float b = .072f * (1f - saturation);
                float[] saturationMatrix = {
                    r + saturation, r, r, 0, g, g + saturation, g, 0,
                    b, b, b + saturation, 0, 0, 0, 0, 1,
                };
                float[] result = new float[16];
                Matrix.multiplyMM(result, 0, combined, 0, saturationMatrix, 0);
                reply.writeNoException();
                reply.writeInt(1);
                reply.writeInt(manager.needsLinearColorMatrix() ? 1 : 0);
                for (float value : result) reply.writeFloat(value);
                return true;
            }
        });
    }
}

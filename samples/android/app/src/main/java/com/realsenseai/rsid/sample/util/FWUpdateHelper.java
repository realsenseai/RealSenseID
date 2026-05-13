// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.lifecycle.MutableLiveData;
import com.realsenseai.rsid.api.DeviceType;
import com.realsenseai.rsid.api.FwUpdater;
import com.realsenseai.rsid.api.Status;
import com.realsenseai.rsid.api.StringVector;
import timber.log.Timber;

/**
 * Wraps {@link FwUpdater} for the sample app's firmware-flash flow. Owns the lifetime of all
 * SWIG-allocated native objects via try/finally — every {@code new} of a SWIG type below has a
 * corresponding {@code delete()} on the cleanup path.
 *
 * <p>Exception: {@link FwUpdater.FwCompatibilityInfo} returned from {@link #checkCompatibility}
 * is owned by the caller, who must call {@code .delete()} when done inspecting it.
 */
public class FWUpdateHelper extends FwUpdater.EventHandler {

  private final MutableLiveData<Integer> progressUpdate;
  private final FWUpdateCallback callback;

  public FWUpdateHelper(@NonNull MutableLiveData<Integer> progressUpdate,
                        @NonNull FWUpdateCallback callback) {
    this.progressUpdate = progressUpdate;
    this.callback = callback;
  }

  @Override
  public void OnProgress(float progress) {
    progressUpdate.postValue(Math.min((int) (progress * 100), 100));
  }

  /**
   * Runs the pre-flash compatibility checks (SKU, secure boot, device type, DB version).
   *
   * @return The compatibility info, or {@code null} if no serial config is available. Caller
   *         owns the returned object and must call {@code .delete()} on it.
   */
  @Nullable
  public FwUpdater.FwCompatibilityInfo checkCompatibility(@NonNull DeviceType deviceType,
                                                          @NonNull String binPath) {
    var config = SDKWrapper.INSTANCE.getSerialConfig();
    if (config == null) {
      return null;
    }

    FwUpdater updater = new FwUpdater(deviceType);
    FwUpdater.Settings settings = new FwUpdater.Settings();
    try {
      settings.setSerial_config(config);
      var info = new FwUpdater.FwCompatibilityInfo();
      updater.CheckCompatibility(settings, binPath, info);
      return info;
    }
    finally {
      settings.delete();
      updater.delete();
    }
  }

  /**
   * Flashes the firmware bin at {@code binPath}. Fires exactly one terminal callback —
   * {@link FWUpdateCallback#onUpdateSuccess()} or {@link FWUpdateCallback#onUpdateFailure(String)}
   * — after {@code UpdateModules} returns. Progress is reported via the LiveData passed to the
   * constructor for the duration of the flash.
   */
  public void flashFirmware(@NonNull DeviceType deviceType, @NonNull String binPath) {
    var config = SDKWrapper.INSTANCE.getSerialConfig();
    if (config == null) {
      callback.onUpdateFailure("Unable to acquire serial port handle.");
      return;
    }

    FwUpdater updater = new FwUpdater(deviceType);
    FwUpdater.Settings settings = new FwUpdater.Settings();
    StringVector moduleNames = new StringVector();
    try {
      settings.setSerial_config(config);
      settings.setForce_full(true);

      String[] outFwVersion = new String[1];
      String[] outRecognitionVersion = new String[1];
      if (!updater.ExtractFwInformation(binPath, outFwVersion, outRecognitionVersion, moduleNames)) {
        callback.onUpdateFailure("Error: Unable to extract FW information from bin file.");
        return;
      }

      Status status = updater.UpdateModules(this, settings, binPath);
      if (status == Status.Ok) {
        callback.onUpdateSuccess();
      }
      else {
        callback.onUpdateFailure("Error: " + status);
      }
    }
    catch (Exception e) {
      Timber.e(e, "Firmware flash threw");
      callback.onUpdateFailure("Error: " + e.getMessage());
    }
    finally {
      moduleNames.delete();
      settings.delete();
      updater.delete();
    }
  }

  public interface FWUpdateCallback {
    void onUpdateSuccess();

    void onUpdateFailure(String message);
  }
}

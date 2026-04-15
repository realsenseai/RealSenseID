// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util;

import static java.util.Objects.isNull;

import android.content.Context;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.lifecycle.MutableLiveData;
import com.realsenseai.rsid.api.DeviceType;
import com.realsenseai.rsid.api.FwUpdater;
import com.realsenseai.rsid.api.Status;
import com.realsenseai.rsid.api.StringVector;
import timber.log.Timber;

public class FWUpdateHelper extends FwUpdater.EventHandler {
  private final Context context;
  private final String TAG = "FWUpdateHelper";
  private final MutableLiveData<Integer> progressUpdate;
  private final FWUpdateCallback callback;

  public FWUpdateHelper(@NonNull Context context,
                        @NonNull MutableLiveData<Integer> progressUpdate,
                        @NonNull FWUpdateCallback callback) {
    this.context = context;
    this.callback = callback;
    this.progressUpdate = progressUpdate;
  }

  @Override
  public void OnProgress(float progress) {
    Timber.tag(TAG).i(String.valueOf(progress));
    var intProgress = Math.min((int)(progress * 100), 100);
    progressUpdate.postValue(intProgress);
    if (intProgress == 100) {
      callback.onUpdateSuccess();
    }
  }

  /**
   * Runs all pre-flash compatibility checks (SKU, secure boot, device type, DB version).
   * Returns the FwCompatibilityInfo, or null if the serial config is unavailable.
   */
  @Nullable
  public FwUpdater.FwCompatibilityInfo checkCompatibility(DeviceType deviceType, @NonNull String binPath) {
    var config = SDKWrapper.INSTANCE.getSerialConfig();
    if (isNull(config)) {
      return null;
    }

    var settings = new FwUpdater.Settings();
    settings.setSerial_config(config);
    var updater = new FwUpdater(deviceType);
    var info = new FwUpdater.FwCompatibilityInfo();
    updater.CheckCompatibility(settings, binPath, info);
    updater.delete();
    settings.delete();
    return info;
  }

  public void flashFirmware(DeviceType deviceType, @NonNull String binPath) {
    var config = SDKWrapper.INSTANCE.getSerialConfig();
    if (isNull(config)) {
      this.callback.onUpdateFailure("Unable to acquire serial portal handle.");
      return;
    }

    var settings = new FwUpdater.Settings();
    settings.setSerial_config(config);
    var updater = new FwUpdater(deviceType);
    var outFwVersion = new String[1];
    var outRecognitionVersion = new String[1];
    var moduleNames = new StringVector();
    settings.setForce_full(true);

    if (updater.ExtractFwInformation(binPath, outFwVersion, outRecognitionVersion, moduleNames)) {
      var status = updater.UpdateModules(this, settings, binPath);
      if (status != Status.Ok) {
        callback.onUpdateFailure("Error: " + status.toString());
        updater.delete();
        settings.delete();
      }
    }
    else {
      callback.onUpdateFailure("Error: Unable to extract FW information from bin file.");
      updater.delete();
      settings.delete();
    }
  }

  public interface FWUpdateCallback {
    void onUpdateSuccess();

    void onUpdateFailure(String message);
  }
}

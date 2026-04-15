// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util;

import static java.util.Objects.isNull;

import android.content.Context;
import android.os.Handler;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import com.realsenseai.rsid.api.DeviceController;
import com.realsenseai.rsid.api.RealSenseID;
import com.realsenseai.rsid.api.Status;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicBoolean;
import timber.log.Timber;

public class CompatibilityChecker {
  private static final String TAG = "CompatibilityChecker";
  private static final String UNKNOWN_VERSION = "unknown";
  private static final String OPFW_PREFIX = "OPFW";
  private static final String VERSION_SEPARATOR = ":";
  private static final String MODEL_SEPARATOR = "\\|";

  private final Handler uiHandler;
  private final ExecutorService executor;
  private final AtomicBoolean isDestroyed = new AtomicBoolean(false);
  private Future<?> currentTask;

  public CompatibilityChecker(@NonNull Context context) {
    this.uiHandler = new Handler(context.getMainLooper());
    this.executor = Executors.newSingleThreadExecutor(r -> {
      Thread thread = new Thread(r, "CompatibilityChecker");
      thread.setDaemon(true);
      return thread;
    });
  }

  /**
   * Check device compatibility asynchronously
   *
   * @param callback Callback to receive results
   */
  public void checkCompatibility(@NonNull CompatibilityCallback callback) {
    if (isDestroyed.get()) {
      Timber.w("CompatibilityChecker is destroyed, ignoring request");
      return;
    }

    // Cancel any existing task
    cancelCurrentTask();

    currentTask = executor.submit(() -> {
      try {
        performCompatibilityCheck(callback);
      }
      catch (Exception e) {
        Timber.e(e, "Unexpected error during compatibility check");
        runOnUiThread(() -> callback.onError("Unexpected error: " + e.getMessage()));
      }
    });
  }

  /**
   * Cancel any ongoing compatibility check
   */
  public void cancel() {
    cancelCurrentTask();
  }

  /**
   * Clean up resources. Should be called when the checker is no longer needed.
   */
  public void destroy() {
    if (isDestroyed.compareAndSet(false, true)) {
      cancelCurrentTask();
      executor.shutdown();
      Timber.d("CompatibilityChecker destroyed");
    }
  }

  private void cancelCurrentTask() {
    if (currentTask != null && !currentTask.isDone()) {
      currentTask.cancel(true);
      currentTask = null;
    }
  }

  private void performCompatibilityCheck(@NonNull CompatibilityCallback callback) {
    if (Thread.currentThread().isInterrupted()) {
      return;
    }

    Timber.d("Starting compatibility check");

    DeviceController controller = null;
    try {
      String firmwareVersion = UNKNOWN_VERSION;
      while (UNKNOWN_VERSION.equals(firmwareVersion)) {
        controller = SDKWrapper.INSTANCE.getDeviceController();
        if (isNull(controller)) {
          Timber.w("Device controller is null");
          runOnUiThread(() -> callback.onError("Device not connected"));
          return;
        }
        firmwareVersion = queryFirmwareVersion(controller);
        SDKWrapper.INSTANCE.closeConnection();
      }
      if (Thread.currentThread().isInterrupted()) {
        return;
      }

      boolean compatible = RealSenseID.IsFwCompatibleWithHost(SDKWrapper.INSTANCE.getDeviceType(), firmwareVersion);

      Timber.d("Compatibility check result: %s (firmware: %s)",
               compatible ? "compatible" : "incompatible", firmwareVersion);

      runOnUiThread(() -> {
        if (compatible) {
          callback.onCompatible();
        }
        else {
          callback.onIncompatible();
        }
      });
    }
    catch (Exception e) {
      Timber.e(e, "Error during compatibility check");
      runOnUiThread(() -> callback.onError("Compatibility check failed: " + e.getMessage()));
    }
    finally {
      // Disconnect in finally block to ensure cleanup
      if (controller != null) {
        try {
          controller.Disconnect();
          controller.delete();
        }
        catch (Exception e) {
          Timber.w(e, "Error disconnecting controller");
        }
      }
    }
  }

  private String queryFirmwareVersion(@NonNull DeviceController controller) {
    String[] firmwareArray = new String[1];
    Status status = controller.QueryFirmwareVersion(firmwareArray);

    if (!Status.Ok.equals(status)) {
      Timber.w("Failed to query firmware version, status: %s", status);
      return UNKNOWN_VERSION;
    }

    String firmwareString = firmwareArray[0];
    if (isNull(firmwareString) || firmwareString.trim().isEmpty()) {
      Timber.w("Received empty firmware version string");
      return UNKNOWN_VERSION;
    }

    return parseFirmwareVersion(firmwareString);
  }

  @VisibleForTesting
  String parseFirmwareVersion(@NonNull String firmwareString) {
    try {
      String[] models = firmwareString.split(MODEL_SEPARATOR);
      for (String model : models) {
        if (model.startsWith(OPFW_PREFIX)) {
          String[] parts = model.split(VERSION_SEPARATOR);
          if (parts.length >= 2) {
            String version = parts[1].trim();
            if (!version.isEmpty()) {
              Timber.d("Parsed firmware version: %s from model: %s", version, model);
              return version;
            }
          }
        }
      }
      Timber.w("No OPFW version found in firmware string: %s", firmwareString);
    }
    catch (Exception e) {
      Timber.e(e, "Error parsing firmware version from: %s", firmwareString);
    }

    return UNKNOWN_VERSION;
  }

  private void runOnUiThread(@NonNull Runnable action) {
    if (isDestroyed.get()) {
      return;
    }

    try {
      uiHandler.post(action);
    }
    catch (Exception e) {
      Timber.e(e, "Failed to post to UI thread");
    }
  }

  /**
   * Check if the checker has been destroyed
   */
  public boolean isDestroyed() {
    return isDestroyed.get();
  }

  public interface CompatibilityCallback {
    void onCompatible();

    void onIncompatible();

    void onError(@Nullable String errorMessage);
  }
}
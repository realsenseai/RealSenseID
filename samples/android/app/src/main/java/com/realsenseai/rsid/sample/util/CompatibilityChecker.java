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
import java.util.function.Consumer;
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
  @Nullable
  private final Consumer<String> rawFirmwareSink;
  private Future<?> currentTask;

  public CompatibilityChecker(@NonNull Context context) {
    this(context, null);
  }

  /**
   * @param rawFirmwareSink Optional sink invoked with the raw firmware version string after a
   *                        successful query, before parsing. Lets callers cache the result so a
   *                        subsequent display (e.g. firmware screen) doesn't need a second query.
   */
  public CompatibilityChecker(@NonNull Context context, @Nullable Consumer<String> rawFirmwareSink) {
    this.uiHandler = new Handler(context.getMainLooper());
    this.rawFirmwareSink = rawFirmwareSink;
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

  private static final int MAX_VERSION_QUERY_ATTEMPTS = 3;
  private static final long VERSION_QUERY_RETRY_DELAY_MS = 500;

  private void performCompatibilityCheck(@NonNull CompatibilityCallback callback) {
    if (Thread.currentThread().isInterrupted()) {
      return;
    }

    Timber.d("Starting compatibility check");

    DeviceController controller = null;
    try {
      // Bounded retry — the old unbounded while-loop combined with closeConnection() on the
      // SDKWrapper singleton would slam the global connection open/closed against any concurrent
      // device caller, and on flaky post-reboot devices it never converged.
      String firmwareVersion = UNKNOWN_VERSION;
      for (int attempt = 0; attempt < MAX_VERSION_QUERY_ATTEMPTS; attempt++) {
        if (Thread.currentThread().isInterrupted()) {
          return;
        }
        if (controller != null) {
          try {
            controller.Disconnect();
            controller.delete();
          }
          catch (Exception e) {
            Timber.w(e, "Error releasing controller between retries");
          }
          controller = null;
        }
        controller = SDKWrapper.INSTANCE.getDeviceController();
        if (isNull(controller)) {
          Timber.w("Device controller is null (attempt %d/%d)", attempt + 1, MAX_VERSION_QUERY_ATTEMPTS);
        }
        else {
          firmwareVersion = queryFirmwareVersion(controller);
          if (!UNKNOWN_VERSION.equals(firmwareVersion)) {
            break;
          }
        }
        if (attempt < MAX_VERSION_QUERY_ATTEMPTS - 1) {
          try {
            Thread.sleep(VERSION_QUERY_RETRY_DELAY_MS);
          }
          catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return;
          }
        }
      }
      if (UNKNOWN_VERSION.equals(firmwareVersion)) {
        Timber.w("Firmware version unknown after %d attempts", MAX_VERSION_QUERY_ATTEMPTS);
        runOnUiThread(() -> callback.onError("Unable to determine firmware version"));
        return;
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

    if (rawFirmwareSink != null) {
      try {
        rawFirmwareSink.accept(firmwareString);
      }
      catch (Exception e) {
        Timber.w(e, "rawFirmwareSink threw");
      }
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
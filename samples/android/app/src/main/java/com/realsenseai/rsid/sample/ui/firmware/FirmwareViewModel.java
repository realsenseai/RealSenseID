// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.firmware;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;
import com.realsenseai.rsid.api.DeviceController;
import com.realsenseai.rsid.api.Status;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import timber.log.Timber;

/**
 * Activity-scoped so the formatted firmware-version HTML survives navigation between fragments.
 * The cache is invalidated on (a) USB attach/detach or (b) successful firmware flash — otherwise
 * the cached value is reused on each visit.
 *
 * <p>{@link #setRawFirmwareVersion(String)} is a write-through used by {@link
 * com.realsenseai.rsid.sample.util.CompatibilityChecker}, which already queries the firmware
 * version during the post-attach compatibility check; populating the cache there means the
 * firmware screen displays instantly on first landing.
 */
public class FirmwareViewModel extends ViewModel {

  private static final String LOADING_TEXT = "Loading...\n";
  private static final String ERROR_LOADING_TEXT = "<ul><li>Error loading firmware info</li></ul>";
  private static final String DEVICE_NOT_CONNECTED_TEXT = "<ul><li>Device not connected</li></ul>";
  private static final String QUERY_FAILED_TEXT = "<ul><li>Failed to query firmware version</li></ul>";

  private final MutableLiveData<String> formattedFirmwareHtml = new MutableLiveData<>();
  private final MutableLiveData<Boolean> isLoading = new MutableLiveData<>(false);

  private final ExecutorService executor = Executors.newSingleThreadExecutor();
  private volatile Future<?> currentLoad;
  private volatile boolean cancelled;
  private volatile boolean firmwareInfoStale = true;

  @NonNull
  public LiveData<String> getFormattedFirmwareHtml() {
    return formattedFirmwareHtml;
  }

  @NonNull
  public LiveData<Boolean> getIsLoading() {
    return isLoading;
  }

  /**
   * Mark the cache stale so the next loadFirmwareInfoIfNeeded() reads from the device.
   */
  public void markFirmwareInfoStale() {
    firmwareInfoStale = true;
  }

  /**
   * Write-through entry: store the raw firmware string already obtained elsewhere (e.g. the
   * compatibility check). Avoids a redundant {@code QueryFirmwareVersion} when the firmware
   * screen is opened immediately after.
   */
  public void setRawFirmwareVersion(@NonNull String raw) {
    formattedFirmwareHtml.postValue(formatFirmwareInfo(raw));
    firmwareInfoStale = false;
  }

  /**
   * Load firmware info only when the cache is stale or empty. Otherwise the existing
   * {@link #getFormattedFirmwareHtml()} value is replayed to new observers automatically.
   */
  public void loadFirmwareInfoIfNeeded() {
    if (!firmwareInfoStale && formattedFirmwareHtml.getValue() != null) {
      return;
    }
    cancelled = false;
    formattedFirmwareHtml.postValue(LOADING_TEXT);
    isLoading.postValue(true);
    currentLoad = executor.submit(this::runLoad);
  }

  /**
   * Force a reload regardless of cache state (used by the refresh button).
   */
  public void reloadFirmwareInfo() {
    markFirmwareInfoStale();
    loadFirmwareInfoIfNeeded();
  }

  /**
   * Cancel the in-flight firmware-info load. Sets the cancellation flag and cancels the Future.
   * Deliberately does <em>not</em> close the CDC connection: QueryFirmwareVersion is a single
   * fast ioctl, so the in-flight call completes on its own and the post-call {@code cancelled}
   * check bails out before posting the result. Closing CDC here would tear down the shared
   * connection out from under any other consumer (e.g. a user-list load that the user is
   * navigating to right when this fragment is being destroyed).
   */
  public void cancelLoad() {
    cancelled = true;
    Future<?> f = currentLoad;
    if (f != null) f.cancel(true);
  }

  private void runLoad() {
    DeviceController controller = null;
    try {
      if (cancelled) return;

      controller = SDKWrapper.INSTANCE.getDeviceController();
      if (controller == null) {
        if (!cancelled) {
          formattedFirmwareHtml.postValue(DEVICE_NOT_CONNECTED_TEXT);
        }
        return;
      }

      String[] fwArr = new String[1];
      Status status = controller.QueryFirmwareVersion(fwArr);
      if (cancelled) return;

      if (status == Status.Ok && fwArr[0] != null) {
        formattedFirmwareHtml.postValue(formatFirmwareInfo(fwArr[0]));
        firmwareInfoStale = false;
        Timber.d("Loaded firmware info");
      }
      else {
        Timber.w("QueryFirmwareVersion failed: %s", status);
        formattedFirmwareHtml.postValue(QUERY_FAILED_TEXT);
      }
    }
    catch (Exception e) {
      if (cancelled) {
        Timber.d("Firmware-info load cancelled: %s", e.getMessage());
      }
      else {
        Timber.e(e, "Failed to load firmware info");
        formattedFirmwareHtml.postValue(ERROR_LOADING_TEXT);
      }
    }
    finally {
      disposeController(controller);
      isLoading.postValue(false);
    }
  }

  @NonNull
  private static String formatFirmwareInfo(@NonNull String firmwareString) {
    StringBuilder html = new StringBuilder("<ul>");
    for (String model : firmwareString.split("\\|")) {
      String[] parts = model.split(":", 2);
      if (parts.length == 2) {
        html.append("<li><b>").append(parts[0]).append("</b>: ").append(parts[1]).append("</li>");
      }
    }
    html.append("</ul>");
    return html.toString();
  }

  private static void disposeController(@Nullable DeviceController c) {
    if (c == null) return;
    try {
      c.Disconnect();
    }
    catch (Exception e) {
      Timber.w(e, "controller.Disconnect");
    }
    try {
      c.delete();
    }
    catch (Exception e) {
      Timber.w(e, "controller.delete");
    }
  }

  @Override
  protected void onCleared() {
    super.onCleared();
    executor.shutdown();
  }
}

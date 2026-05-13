// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.shared;

import static com.realsenseai.rsid.api.DeviceType.F45x;
import static com.realsenseai.rsid.api.DeviceType.F50x;
import static com.realsenseai.rsid.api.DeviceType.Unknown;

import android.os.Handler;
import android.os.Looper;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;
import com.realsenseai.rsid.api.DeviceConfig;
import com.realsenseai.rsid.api.DeviceType;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import timber.log.Timber;

public class RealSenseIdSharedViewModel extends ViewModel {
  // Device Type Management
  private final MutableLiveData<DeviceType> deviceType = new MutableLiveData<>();
  private final MutableLiveData<Boolean> usbConnectionState = new MutableLiveData<>();
  private final MutableLiveData<String> deviceInfo = new MutableLiveData<>();
  private final MutableLiveData<Boolean> isDeviceCompatible = new MutableLiveData<>();

  // Settings shared variables
  private final MutableLiveData<Boolean> useHostDb = new MutableLiveData<>();
  private final MutableLiveData<DeviceConfig.DumpMode> dumpMode = new MutableLiveData<>();
  private final MutableLiveData<DeviceConfig.SecurityLevel> securityLevel = new MutableLiveData<>();
  private final MutableLiveData<DeviceConfig.AlgoFlow> operationMode = new MutableLiveData<>();
  private final MutableLiveData<DeviceConfig.CameraRotation> cameraRotation = new MutableLiveData<>();

  // Additional shared state
  private final MutableLiveData<Boolean> isOperationInProgress = new MutableLiveData<>();
  private final MutableLiveData<Boolean> isCompatibilityCheckRunning = new MutableLiveData<>(false);
  private final MutableLiveData<Boolean> oneToOneMode = new MutableLiveData<>();
  // List of callbacks to notify when device is attached
  private final List<DeviceAttachmentCallback> deviceAttachmentCallbacks = new ArrayList<>();
  // Background executor for settings loading
  private final ExecutorService executor = Executors.newSingleThreadExecutor();
  // Device settings cache state
  private boolean deviceSettingsLoaded = false;

  public RealSenseIdSharedViewModel() {
    // Initialize with default values
    deviceType.setValue(Unknown);
    usbConnectionState.setValue(false);
    deviceInfo.setValue("");
    isDeviceCompatible.setValue(false);
    isOperationInProgress.setValue(false);
    oneToOneMode.setValue(false);

    Timber.d("RealSenseIdSharedViewModel initialized");
  }
  // Device settings cache state

  // Device Type Methods
  @NonNull
  public LiveData<DeviceType> getDeviceType() {
    return deviceType;
  }

  public void setDeviceType(@NonNull DeviceType type) {
    if (getCurrentDeviceType() != type) {
      Timber.d("Device type changed from %s to %s", getCurrentDeviceType(), type);
      deviceType.setValue(type);

      // Update device info when type changes
      updateDeviceInfo(type);

      // Reset compatibility when device type changes
      setDeviceCompatible(false);
    }
  }

  @NonNull
  public DeviceType getCurrentDeviceType() {
    DeviceType current = deviceType.getValue();
    return current != null ? current : Unknown;
  }

  public boolean isDeviceTypeSupported() {
    DeviceType current = getCurrentDeviceType();
    return current == F45x || current == F50x;
  }

  public boolean isF45xDevice() {
    return getCurrentDeviceType() == F45x;
  }

  public boolean isF50xDevice() {
    return getCurrentDeviceType() == F50x;
  }

  public boolean isUnknownDevice() {
    return getCurrentDeviceType() == Unknown;
  }

  // USB Connection State Methods
  @NonNull
  public LiveData<Boolean> getUsbConnectionState() {
    return usbConnectionState;
  }

  public void setUsbConnectionState(boolean isConnected) {
    Boolean currentState = usbConnectionState.getValue();
    if (currentState == null || currentState != isConnected) {
      Timber.d("USB connection state changed to: %s", isConnected);
      usbConnectionState.setValue(isConnected);

      // Reset device type if disconnected
      if (!isConnected) {
        setDeviceType(Unknown);
      }
    }
  }

  public boolean isUsbConnected() {
    Boolean connected = usbConnectionState.getValue();
    return connected != null && connected;
  }

  // Device Info Methods
  @NonNull
  public LiveData<String> getDeviceInfo() {
    return deviceInfo;
  }

  public void setDeviceInfo(@Nullable String info) {
    deviceInfo.setValue(info != null ? info : "");
  }

  @NonNull
  public String getCurrentDeviceInfo() {
    String info = deviceInfo.getValue();
    return info != null ? info : "";
  }

  // Use Host DB
  @NonNull
  public LiveData<Boolean> getUseHostDb() {
    return useHostDb;
  }

  public void setUseHostDb(boolean value) {
    Boolean currentDbIsHost = useHostDb.getValue();
    if (currentDbIsHost == null || currentDbIsHost != value) {
      Timber.d("Use HostDB changed to: %s", value);
      useHostDb.setValue(value);
    }
  }

  // Dump Mode Methods
  public void setDumpMode(@NonNull DeviceConfig.DumpMode mode) {
    DeviceConfig.DumpMode currentMode = getCurrentDumpMode();
    if (currentMode != mode) {
      Timber.d("Dump mode changed from %s to %s", currentMode, mode);
      dumpMode.setValue(mode);
    }
  }

  @NonNull
  public DeviceConfig.DumpMode getCurrentDumpMode() {
    DeviceConfig.DumpMode current = dumpMode.getValue();
    return current != null ? current : DeviceConfig.DumpMode.None;
  }

  // Security Level Methods
  @NonNull
  public LiveData<DeviceConfig.SecurityLevel> getSecurityLevel() {
    return securityLevel;
  }

  public void setSecurityLevel(@NonNull DeviceConfig.SecurityLevel level) {
    DeviceConfig.SecurityLevel currentLevel = securityLevel.getValue();
    if (currentLevel != level) {
      Timber.d("Security level changed from %s to %s", currentLevel, level);
      securityLevel.setValue(level);
    }
  }

  @NonNull
  public DeviceConfig.SecurityLevel getCurrentSecurityLevel() {
    DeviceConfig.SecurityLevel current = securityLevel.getValue();
    return current != null ? current : DeviceConfig.SecurityLevel.High;
  }

  // Operation Mode (AlgoFlow) Methods
  @NonNull
  public LiveData<DeviceConfig.AlgoFlow> getOperationMode() {
    return operationMode;
  }

  public void setOperationMode(@NonNull DeviceConfig.AlgoFlow mode) {
    DeviceConfig.AlgoFlow currentMode = operationMode.getValue();
    if (currentMode != mode) {
      Timber.d("Operation mode changed from %s to %s", currentMode, mode);
      operationMode.setValue(mode);
    }
  }

  @NonNull
  public DeviceConfig.AlgoFlow getCurrentOperationMode() {
    DeviceConfig.AlgoFlow current = operationMode.getValue();
    return current != null ? current : DeviceConfig.AlgoFlow.All;
  }

  // Camera Rotation Methods
  @NonNull
  public LiveData<DeviceConfig.CameraRotation> getCameraRotation() {
    return cameraRotation;
  }

  public void setCameraRotation(@NonNull DeviceConfig.CameraRotation rotation) {
    DeviceConfig.CameraRotation currentRotation = cameraRotation.getValue();
    if (currentRotation != rotation) {
      Timber.d("Camera rotation changed from %s to %s", currentRotation, rotation);
      cameraRotation.setValue(rotation);
    }
  }

  @NonNull
  public DeviceConfig.CameraRotation getCurrentCameraRotation() {
    DeviceConfig.CameraRotation current = cameraRotation.getValue();
    return current != null ? current : DeviceConfig.CameraRotation.Rotation_0_Deg;
  }

  // Device Compatibility Methods
  @NonNull
  public LiveData<Boolean> getDeviceCompatibility() {
    return isDeviceCompatible;
  }

  public void setDeviceCompatible(boolean compatible) {
    Boolean currentCompatibility = isDeviceCompatible.getValue();
    if (currentCompatibility == null || currentCompatibility != compatible) {
      Timber.d("Device compatibility changed to: %s", compatible);
      isDeviceCompatible.setValue(compatible);
    }
  }

  /**
   * Reset compatibility to "unverified" (null). Use this when the existing verdict is no longer
   * valid but we haven't determined a new one — e.g. after a firmware flash, where the new
   * firmware's compatibility is unknown until the post-reboot {@code checkCompatibility} runs.
   * Distinct from {@link #setDeviceCompatible}{@code (false)}, which means "we determined the
   * device is incompatible".
   */
  public void clearDeviceCompatibility() {
    if (isDeviceCompatible.getValue() != null) {
      Timber.d("Device compatibility cleared (unverified)");
      isDeviceCompatible.setValue(null);
    }
  }

  public boolean isCurrentDeviceCompatible() {
    Boolean compatible = isDeviceCompatible.getValue();
    return compatible != null && compatible;
  }

  // Operation State Methods
  @NonNull
  public LiveData<Boolean> getOperationInProgress() {
    return isOperationInProgress;
  }

  public void setOperationInProgress(boolean inProgress) {
    Boolean currentState = isOperationInProgress.getValue();
    if (currentState == null || currentState != inProgress) {
      Timber.d("Operation in progress state changed to: %s", inProgress);
      isOperationInProgress.setValue(inProgress);
    }
  }

  public boolean isAnyOperationInProgress() {
    Boolean inProgress = isOperationInProgress.getValue();
    return inProgress != null && inProgress;
  }

  public LiveData<Boolean> getCompatibilityCheckRunning() {
    return isCompatibilityCheckRunning;
  }

  public void setCompatibilityCheckRunning(boolean running) {
    isCompatibilityCheckRunning.postValue(running);
  }

  // One-to-One Mode Methods
  @NonNull
  public LiveData<Boolean> getOneToOneMode() {
    return oneToOneMode;
  }

  public void setOneToOneMode(boolean enabled) {
    Boolean currentMode = oneToOneMode.getValue();
    if (currentMode == null || currentMode != enabled) {
      Timber.d("One-to-one mode changed to: %s", enabled);
      oneToOneMode.setValue(enabled);
    }
  }

  public boolean isOneToOneModeEnabled() {
    Boolean enabled = oneToOneMode.getValue();
    return enabled != null && enabled;
  }

  // Utility Methods
  public void resetDeviceState() {
    Timber.d("Resetting device state");
    setDeviceType(Unknown);
    SDKWrapper.INSTANCE.setDeviceType(Unknown);
    setUsbConnectionState(false);
    setDeviceInfo("");
    setDeviceCompatible(false);
    setOperationInProgress(false);

    // Clear device settings cache and compatibility state
    clearDeviceSettingsCache();
    setCompatibilityCheckRunning(false);

    // Notify all registered callbacks about device detachment
    for (DeviceAttachmentCallback callback : deviceAttachmentCallbacks) {
      try {
        callback.onDeviceAttached(); // This will cause UI to update to "no
        // device" state
      }
      catch (Exception e) {
        Timber.e(e, "Error calling device attachment callback during reset");
      }
    }
  }

  public void handleDeviceAttached(@NonNull DeviceType attachedDeviceType) {
    Timber.d("Device attached: %s", attachedDeviceType);
    SDKWrapper.INSTANCE.setDeviceType(attachedDeviceType);
    setUsbConnectionState(true);
    setDeviceType(attachedDeviceType);
  }

  public void handleDeviceDetached() {
    Timber.d("Device detached");
    resetDeviceState();
  }

  // Device capabilities based on type
  public boolean supportsFirmwareFlash() {
    return isF45xDevice(); // TODO: Add support for F50x
  }

  public boolean supportsDeviceController() {
    return isDeviceTypeSupported();
  }

  private void updateDeviceInfo(@NonNull DeviceType type) {
    String info;
    if (type == DeviceType.F45x) {
      info = "RealSense F45x Device";
    }
    else if (type == DeviceType.F50x) {
      info = "RealSense F50x Device";
    }
    else if (type == DeviceType.Unknown) {
      info = "Unknown Device";
    }
    else {
      info = "Unsupported Device";
    }
    setDeviceInfo(info);
  }

  // Device settings cache management
  public boolean areDeviceSettingsLoaded() {
    return deviceSettingsLoaded;
  }

  public void markDeviceSettingsAsLoaded() {
    deviceSettingsLoaded = true;
    Timber.d("Device settings marked as loaded in SharedViewModel cache. Notifying %d callbacks.", deviceAttachmentCallbacks.size());

    // Post the callback notification to the main thread to ensure LiveData
    // updates are processed first
    new Handler(Looper.getMainLooper()).post(() -> {
      // Notify all registered callbacks
      for (DeviceAttachmentCallback callback : deviceAttachmentCallbacks) {
        try {
          Timber.d("Calling device attachment callback");
          callback.onDeviceAttached();
        }
        catch (Exception e) {
          Timber.e(e, "Error calling device attachment callback");
        }
      }
    });
  }

  /**
   * Register a callback to be notified when device settings are loaded
   */
  public void registerDeviceAttachmentCallback(DeviceAttachmentCallback callback) {
    if (callback != null && !deviceAttachmentCallbacks.contains(callback)) {
      deviceAttachmentCallbacks.add(callback);
      Timber.d("Registered device attachment callback. Total callbacks: %d", deviceAttachmentCallbacks.size());

      // If device settings are already loaded, immediately trigger the callback
      if (deviceSettingsLoaded) {
        Timber.d("Device settings already loaded, immediately triggering callback");
        // Use Handler to ensure this runs after any pending LiveData updates
        new Handler(Looper.getMainLooper()).post(() -> {
          try {
            callback.onDeviceAttached();
            Timber.d("Immediately triggered callback for already attached device");
          }
          catch (Exception e) {
            Timber.e(e, "Error calling immediate device attachment callback");
          }
        });
      }
      else {
        Timber.d("Device settings not yet loaded, callback will be triggered later");
      }
    }
    else {
      Timber.w("Callback registration failed - callback is null or already registered");
    }
  }

  /**
   * Unregister a device attachment callback
   */
  public void unregisterDeviceAttachmentCallback(DeviceAttachmentCallback callback) {
    if (callback != null) {
      deviceAttachmentCallbacks.remove(callback);
      Timber.d("Unregistered device attachment callback");
    }
  }

  public void clearDeviceSettingsCache() {
    deviceSettingsLoaded = false;
    Timber.d("Device settings cache cleared in SharedViewModel");
  }

  // Device readiness validation
  public boolean isDeviceReadyForSettings() {
    return isUsbConnected() && isCurrentDeviceCompatible() && areDeviceSettingsLoaded() && !isAnyOperationInProgress();
  }

  public boolean isDeviceMinimallyReady() {
    return isUsbConnected() && isCurrentDeviceCompatible();
  }

  // Debug/Logging methods
  public void logCurrentState() {
    Timber.d("=== Shared ViewModel State ===");
    Timber.d("Device Type: %s", getCurrentDeviceType());
    Timber.d("USB Connected: %s", isUsbConnected());
    Timber.d("Device Compatible: %s", isCurrentDeviceCompatible());
    Timber.d("Operation In Progress: %s", isAnyOperationInProgress());
    Timber.d("Device Info: %s", getCurrentDeviceInfo());
    Timber.d("=============================");
  }

  public void loadDeviceSettingsInBackground() {
    executor.submit(this::loadDeviceSettingsOnDeviceAttached);
  }

  void loadDeviceSettingsOnDeviceAttached() {
    FaceAuthenticator authenticator = null;
    try {
      // Initialize with safe defaults first (in case device loading fails)
      dumpMode.postValue(DeviceConfig.DumpMode.None);
      securityLevel.postValue(DeviceConfig.SecurityLevel.High);
      operationMode.postValue(DeviceConfig.AlgoFlow.All);
      cameraRotation.postValue(DeviceConfig.CameraRotation.Rotation_0_Deg);

      authenticator = SDKWrapper.INSTANCE.getAuthenticator();
      if (authenticator == null) {
        Timber.w("Cannot load device settings on startup - authenticator not available");
        return;
      }

      DeviceConfig config = new DeviceConfig();
      var status = authenticator.QueryDeviceConfig(config);

      if (status != com.realsenseai.rsid.api.Status.Ok) {
        Timber.w("Failed to query device config on startup: %s", status);
        return;
      }

      // Update shared ViewModel settings using postValue (thread-safe)
      dumpMode.postValue(config.getDump_mode());
      securityLevel.postValue(config.getSecurity_level());
      operationMode.postValue(config.getAlgo_flow());
      cameraRotation.postValue(config.getCamera_rotation());

      Timber.d("SharedViewModel: Successfully loaded device settings on device attached - "
               + "DumpMode: %s, SecurityLevel: %s, OperationMode: %s, CameraRotation: %s",
               config.getDump_mode(), config.getSecurity_level(), config.getAlgo_flow(), config.getCamera_rotation());
    }
    catch (Exception e) {
      Timber.e(e, "SharedViewModel: Error loading device settings on startup");
      // Initialize with safe defaults if loading fails
      try {
        dumpMode.postValue(DeviceConfig.DumpMode.None);
        securityLevel.postValue(DeviceConfig.SecurityLevel.High);
        operationMode.postValue(DeviceConfig.AlgoFlow.All);
        cameraRotation.postValue(DeviceConfig.CameraRotation.Rotation_0_Deg);
      }
      catch (Exception e2) {
        Timber.e(e2, "Failed to set even default values - native library may not be loaded");
      }
    }
    finally {
      // Release the authenticator (the native handle) before marking settings as loaded — the
      // callbacks fired by markDeviceSettingsAsLoaded build their own native handles, and only one
      // can be Connected to the device at a time. The CDC pipe stays open: SDKWrapper caches it
      // across operations so subsequent fragments (firmware, users, preview) start fast.
      if (authenticator != null) {
        try {
          authenticator.Disconnect();
          authenticator.delete();
        }
        catch (Exception e) {
          Timber.w(e, "Error disconnecting authenticator after settings load");
        }
      }

      // Mark settings as loaded — this triggers device attachment callbacks
      markDeviceSettingsAsLoaded();
    }
  }

  @Override
  protected void onCleared() {
    super.onCleared();

    // Shutdown executor gracefully
    if (executor != null && !executor.isShutdown()) {
      executor.shutdown();
      try {
        if (!executor.awaitTermination(1000, java.util.concurrent.TimeUnit.MILLISECONDS)) {
          executor.shutdownNow();
        }
      }
      catch (InterruptedException e) {
        executor.shutdownNow();
        Thread.currentThread().interrupt();
      }
    }

    Timber.d("RealSenseIdSharedViewModel cleared");
  }

  // Callback interface for device attachment events
  public interface DeviceAttachmentCallback {
    void onDeviceAttached();
  }
}

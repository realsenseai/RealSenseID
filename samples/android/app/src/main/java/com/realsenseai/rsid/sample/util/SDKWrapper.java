// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util;

import static java.util.Objects.nonNull;
import static java.util.Objects.requireNonNull;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.realsenseai.rsid.api.DeviceController;
import com.realsenseai.rsid.api.DeviceType;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.api.SerialConfig;
import com.realsenseai.rsid.api.Status;
import com.realsenseai.rsid.impl.UsbCdcConnection;
import com.realsenseai.rsid.sample.MainApplication;
import com.realsenseai.rsid.sample.ui.shared.RealSenseIdSharedViewModel;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiConsumer;
import java.util.regex.Pattern;
import timber.log.Timber;

public enum SDKWrapper {
  INSTANCE;

  // Static initialization
  static {
    INSTANCE.initialize();
  }

  @NonNull
  public final Object Lock = new Object();
  private final Set<ConnectionListener> listeners = ConcurrentHashMap.newKeySet();
  // Instance variables
  @Nullable
  private UsbCdcConnection connection;
  private volatile ConnectionState connectionState = ConnectionState.DISCONNECTED;
  private final AtomicBoolean permissionRequestInFlight = new AtomicBoolean(false);
  @Nullable
  private volatile Runnable pendingOnPermissionGranted;
  private Config config;
  private RealSenseIdSharedViewModel sharedViewModel;
  private DeviceType deviceType = DeviceType.Unknown;

  private void initialize() {
    try {
      this.config = new Config(MainApplication.getInstance().getApplicationContext());
      Timber.d("SDKWrapper initialized with default configuration");
    }
    catch (Exception e) {
      Timber.e(e, "Failed to initialize SDKWrapper");
      connectionState = ConnectionState.ERROR;
    }
  }

  @NonNull
  public DeviceType getDeviceType() {
    return deviceType;
  }

  public void setDeviceType(@NonNull DeviceType type) {
    deviceType = type;
  }

  /**
   * Configure the SDK wrapper with custom settings
   */
  public SDKWrapper configure(@NonNull Config config) {
    synchronized (Lock) {
      this.config = requireNonNull(config, "Config cannot be null");
      Timber.d("SDKWrapper configured with custom settings");
      return this;
    }
  }

  /**
   * Get current connection state
   */
  @NonNull
  public ConnectionState getConnectionState() {
    return connectionState;
  }

  /**
   * Check if currently connected
   */
  public boolean isConnected() {
    return connectionState == ConnectionState.CONNECTED && connection != null;
  }

  /**
   * Add connection listener
   */
  public void addConnectionListener(@NonNull ConnectionListener listener) {
    listeners.add(requireNonNull(listener, "Listener cannot be null"));
  }

  /**
   * Remove connection listener
   */
  public void removeConnectionListener(@NonNull ConnectionListener listener) {
    listeners.remove(requireNonNull(listener, "Listener cannot be null"));
  }

  /**
   * Clear all listeners
   */
  public void clearConnectionListeners() {
    listeners.clear();
  }

  private void notifyListeners(BiConsumer<ConnectionListener, Void> action) {
    listeners.forEach(listener -> {
      try {
        action.accept(listener, null);
      }
      catch (Exception e) {
        Timber.e(e, "Error notifying connection listener");
      }
    });
  }

  /**
   * Check if a RealSense device is currently attached and accessible
   */
  public boolean isDeviceAvailable() {
    if (config == null) {
      return false;
    }

    try {
      UsbCdcConnection tempConnection = new UsbCdcConnection();
      boolean found = tempConnection.FindSupportedDevice(config.getContext());
      try {
        tempConnection.CloseConnection();
      }
      catch (Exception e) {
        // Ignore close errors for temp connection
      }
      return found;
    }
    catch (Exception e) {
      Timber.w(e, "Error checking device availability");
      return false;
    }
  }

  public void ensurePermissionThenRun(@NonNull Context context, @NonNull Runnable onGranted) {
    if (!permissionRequestInFlight.compareAndSet(false, true)) {
      Timber.d("Permission request already in flight, skipping duplicate");
      return;
    }

    UsbCdcConnection usbCdcConnection = new UsbCdcConnection();
    if (!usbCdcConnection.FindSupportedDevice(context)) {
      Timber.e("No supported device found for permission request");
      permissionRequestInFlight.set(false);
      return;
    }

    // Request permission directly. If the system auto-grant from the manifest
    // USB filter hasn't processed yet, a dialog will show. If the user denies
    // (or the dialog is dismissed), we store onGranted as a pending action.
    // When connectToDeviceSingle later succeeds in getting permission, it will
    // fire the pending action to resume the standard device attachment path.
    usbCdcConnection.RequestDevicePermission(context, granted -> {
      permissionRequestInFlight.set(false);
      if (granted) {
        Timber.d("USB permission granted, proceeding");
        pendingOnPermissionGranted = null;
        onGranted.run();
      }
      else {
        Timber.e("USB permission denied — will retry when connectToDeviceSingle gets permission");
        pendingOnPermissionGranted = onGranted;
      }
    });
  }

  /**
   * Force a device refresh - useful when USB state might have changed
   */
  public void refreshDeviceState() {
    synchronized (Lock) {
      Timber.d("Refreshing device state...");

      // Close existing connection
      closeConnection();

      // Wait a moment for USB subsystem to settle
      try {
        Thread.sleep(500);
      }
      catch (InterruptedException e) {
        Thread.currentThread().interrupt();
      }

      // Check if device is available
      if (isDeviceAvailable()) {
        Timber.d("Device is available after refresh");
      }
      else {
        Timber.w("Device not available after refresh");
      }
    }
  }

  // Helper method to verify device availability
  private boolean isDeviceStillAvailable() {
    try {
      // This is a simple check - you might need to implement actual device verification
      // based on your UsbCdcConnection API
      return connection != null;
    }
    catch (Exception e) {
      Timber.w(e, "Error checking device availability");
      return false;
    }
  }

  private boolean connectToDeviceWithRetry() {
    if (config == null) {
      Timber.e("No configuration available for connection");
      return false;
    }

    for (int attempt = 1; attempt <= config.getMaxRetryAttempts(); attempt++) {
      try {
        Timber.d("Connection attempt %d of %d", attempt, config.getMaxRetryAttempts());

        if (connectToDeviceSingle(config.getContext())) {
          Timber.d("Connection successful on attempt %d", attempt);
          return true;
        }
      }
      catch (Exception e) {
        Timber.w(e, "Connection attempt %d failed", attempt);
      }

      if (attempt < config.getMaxRetryAttempts()) {
        try {
          Thread.sleep(config.getRetryDelayMs());
        }
        catch (InterruptedException ie) {
          Thread.currentThread().interrupt();
          Timber.w("Connection retry interrupted");
          return false;
        }
      }
    }

    Timber.e("All connection attempts failed");
    return false;
  }

  private boolean connectToDeviceSingle(Context context) {
    synchronized (Lock) {
      // If ensurePermissionThenRun is already handling permission, skip to avoid
      // duplicate dialogs and spurious "denied" toasts.
      if (permissionRequestInFlight.get()) {
        Timber.d("Permission request in flight, skipping connection attempt");
        return false;
      }

      try {
        connection = new UsbCdcConnection();

        // First check if device is found
        boolean deviceFound = connection.FindSupportedDevice(context);
        if (!deviceFound) {
          Timber.e("No supported RealSense device found");
          connection = null;
          return false;
        }

        final boolean[] permissionGranted = {false};
        final boolean[] callbackReceived = {false};
        final Object permissionLock = new Object();

        connection.RequestDevicePermission(context, granted -> {
          synchronized (permissionLock) {
            permissionGranted[0] = granted;
            callbackReceived[0] = true;
            String message = granted ? "USB permission granted" : "USB permission denied";

            if (!granted) {
              Timber.e(message);
              Toast.makeText(context, message, Toast.LENGTH_LONG).show();
            }
            else {
              Timber.i(message);
              // If ensurePermissionThenRun was previously denied, resume its path now
              Runnable pending = pendingOnPermissionGranted;
              if (pending != null) {
                pendingOnPermissionGranted = null;
                Timber.d("Permission now granted — running pending device attachment action");
                new Handler(Looper.getMainLooper()).post(pending);
              }
            }

            permissionLock.notify();
          }
        });

        // Wait for permission response (with timeout)
        synchronized (permissionLock) {
          try {
            if (!callbackReceived[0]) {
              permissionLock.wait(config.getConnectionTimeoutMs());
            }
          }
          catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            Timber.w("Permission request interrupted");
            return false;
          }
        }

        if (!callbackReceived[0]) {
          Timber.e("USB permission request timed out");
          connection.CloseConnection();
          connection = null;
          // Delay before next retry to prevent rapid permission requests
          try {
            Thread.sleep(1000);
          }
          catch (InterruptedException ie) {
            Thread.currentThread().interrupt();
          }
          return false;
        }

        if (!permissionGranted[0]) {
          Timber.e("USB permission was denied by user");
          connection.CloseConnection();
          connection = null;
          return false;
        }

        // Verify device is still available before proceeding
        if (!isDeviceStillAvailable()) {
          Timber.e("Device became unavailable after permission granted");
          connection.CloseConnection();
          connection = null;
          return false;
        }

        return true;
      }
      catch (Exception e) {
        Timber.e(e, "Error during device connection");
        if (connection != null) {
          try {
            connection.CloseConnection();
          }
          catch (Exception closeException) {
            Timber.w(closeException, "Error closing connection after failure");
          }
          connection = null;
        }
        return false;
      }
    }
  }

  public boolean openUVCConnection() {
    if (connectionState != ConnectionState.CONNECTED) {
      Timber.e("Connection not established without prior CDC connection");
      return false;
    }
    return requireNonNull(connection).OpenUvcConnection();
  }

  public int getUVCFileDescriptor() {
    if (connection != null) {
      return connection.GetUvcFileDescriptor();
    }
    return -1;
  }

  public void closeUVCConnection() {
    if (connection != null) {
      connection.CloseUvcConnection();
    }
  }

  @Nullable
  public SerialConfig getSerialConfig() {
    synchronized (Lock) {
      if (connectionState == ConnectionState.CONNECTING) {
        Timber.d("Connection already in progress, waiting...");
        return null;
      }
      connectionState = ConnectionState.CONNECTING;
      notifyListeners((listener, unused) -> {
        // Note: We don't have a connecting callback, but could add one
      });

      try {
        if (config == null) {
          throw new IllegalStateException("SDKWrapper not properly configured");
        }

        boolean connected = config.isAutoReconnect() ?
                            connectToDeviceWithRetry() :
                            connectToDeviceSingle(config.getContext());

        if (!connected || connection == null) {
          Timber.e("Device connection failed or permission denied");
          connectionState = ConnectionState.ERROR;
          notifyListeners((listener, unused) -> listener.onError("Device connection failed"));
          connection = null;
          return null;
        }

        // Try to open the connection with better error handling
        boolean connectionOpened = false;
        try {
          connectionOpened = connection.OpenConnection();
        }
        catch (IllegalArgumentException e) {
          if (e.getMessage() != null && e.getMessage().contains("does not exist or is restricted")) {
            Timber.e("USB device access denied or device disconnected: %s", e.getMessage());
            connectionState = ConnectionState.ERROR;
            notifyListeners(
              (listener, unused) -> listener.onError("USB device access denied. Please reconnect device and grant permission."));
          }
          else {
            Timber.e(e, "Failed to open USB connection");
            connectionState = ConnectionState.ERROR;
            notifyListeners((listener, unused) -> listener.onError("Failed to open USB connection: " + e.getMessage()));
          }
          connection.CloseConnection();
          connection = null;
          return null;
        }
        catch (Exception e) {
          Timber.e(e, "Unexpected error opening connection");
          connectionState = ConnectionState.ERROR;
          notifyListeners((listener, unused) -> listener.onError("Connection error: " + e.getMessage()));
          connection.CloseConnection();
          connection = null;
          return null;
        }

        if (!connectionOpened) {
          Timber.e("Unable to open USB connection");
          connectionState = ConnectionState.ERROR;
          notifyListeners((listener, unused) -> listener.onError("Unable to open USB connection"));
          connection.CloseConnection();
          connection = null;
          return null;
        }

        SerialConfig serialConfig = new SerialConfig();
        serialConfig.setFileDescriptor(connection.GetFileDescriptor());
        serialConfig.setReadEndpoint(connection.GetReadEndpointAddress());
        serialConfig.setWriteEndpoint(connection.GetWriteEndpointAddress());

        connectionState = ConnectionState.CONNECTED;
        notifyListeners((listener, unused) -> listener.onConnected());

        Timber.d("Serial config obtained successfully");
        return serialConfig;
      }
      catch (Exception e) {
        Timber.e(e, "Failed to get serial config");
        connectionState = ConnectionState.ERROR;
        notifyListeners((listener, unused) -> listener.onError("Failed to get serial config: " + e.getMessage()));

        if (connection != null) {
          try {
            connection.CloseConnection();
          }
          catch (Exception closeException) {
            Timber.w(closeException, "Error closing connection after failure");
          }
          connection = null;
        }
        return null;
      }
    }
  }

  @Nullable
  public FaceAuthenticator getAuthenticator() {
    synchronized (this) {
      try {
        FaceAuthenticator authenticator = new FaceAuthenticator(getDeviceType());   // FIXME: Use correct device type
        SerialConfig serialConfig = getSerialConfig();

        if (serialConfig != null) {
          authenticator.Connect(serialConfig);
          Timber.d("Face authenticator created and connected");
          return authenticator;
        }
        else {
          Timber.e("Cannot create authenticator - no serial config available");
          return null;
        }
      }
      catch (Exception e) {
        Timber.e(e, "Error creating face authenticator");
        return null;
      }
    }
  }

  public void disposeAuthenticator(FaceAuthenticator authenticator) {
    synchronized (this) {
      authenticator.Disconnect();
      authenticator.delete();
    }
  }

  @Nullable
  public DeviceController getDeviceController() {
    try {
      DeviceController deviceController = new DeviceController(getDeviceType());
      SerialConfig serialConfig = getSerialConfig();

      if (serialConfig != null) {
        deviceController.Connect(serialConfig);
        Timber.d("Device controller created and connected");
        return deviceController;
      }
      else {
        Timber.e("Cannot create device controller - no serial config available");
        return null;
      }
    }
    catch (Exception e) {
      Timber.e(e, "Error creating device controller");
      return null;
    }
  }

  public Integer getSKUVersion() throws RuntimeException {
    DeviceController controller = getDeviceController(); // SKU version is relevant for F45x only.
    if (controller == null) {
      throw new RuntimeException("Unable to obtain device controller handle");
    }

    try {
      short[] asciiArr = new short[1];
      Status status = controller.QueryOtpVersion(asciiArr);

      if (status == Status.Ok) {
        int deviceSkuVer = (int)asciiArr[0] - (int)'0' + 1;
        Timber.i("Using OTP: deviceSkuVer = %s", deviceSkuVer);
        return deviceSkuVer;
      }
      else {
        String[] snStrArray = new String[1];
        status = controller.QuerySerialNumber(snStrArray);

        if (status == Status.Ok) {
          String sn = snStrArray[0];
          Pattern reg1 = Pattern.compile("12[02]\\d6228\\d{4}.*");
          Pattern reg2 = Pattern.compile("\\d{4}6229\\d{4}.*");
          Pattern reg3 = Pattern.compile("\\d{4}62[3-9]\\d{5}.*");

          if (reg1.matcher(sn).matches() || reg2.matcher(sn).matches() || reg3.matcher(sn).matches()) {
            Timber.i("SKU version determined from serial number: 2");
            return 2;
          }
          else {
            Timber.i("SKU version determined from serial number: 1");
            return 1;
          }
        }
      }

      throw new RuntimeException("Unable to get SKU version. Tried OTP and SN methods.");
    }
    finally {
      try {
        controller.Disconnect();
      }
      catch (Exception e) {
        Timber.w(e, "Error disconnecting device controller");
      }
    }
  }

  public void closeConnection() {
    synchronized (Lock) {
      if (connection != null) {
        try {
          connection.CloseConnection();
          Timber.d("Connection closed successfully");
        }
        catch (Exception e) {
          Timber.e(e, "Error closing connection");
        }
        finally {
          connection = null;
          ConnectionState previousState = connectionState;
          connectionState = ConnectionState.DISCONNECTED;

          if (previousState == ConnectionState.CONNECTED) {
            notifyListeners((listener, unused) -> listener.onDisconnected());
          }
        }
      }
    }
  }

  /**
   * Force reconnection - useful when device state might have changed
   */
  public boolean reconnect() {
    synchronized (Lock) {
      Timber.d("Forcing reconnection...");
      closeConnection();
      return nonNull(getSerialConfig());
    }
  }

  /**
   * Cleanup all resources and reset state
   */
  public void cleanup() {
    synchronized (Lock) {
      Timber.d("Cleaning up SDKWrapper resources");
      closeConnection();
      clearConnectionListeners();
      // Reset to disconnected state
      connectionState = ConnectionState.DISCONNECTED;
    }
  }

  /**
   * Get diagnostic information about current state
   */
  @NonNull
  public String getDiagnosticInfo() {
    synchronized (Lock) {
      StringBuilder info = new StringBuilder();
      info.append("SDKWrapper Diagnostic Info:\n");
      info.append("Connection State: ").append(connectionState).append("\n");
      info.append("Connection Object: ").append(connection != null ? "Present" : "Null").append("\n");
      info.append("Config: ").append(config != null ? "Present" : "Null").append("\n");
      info.append("Listeners: ").append(listeners.size()).append("\n");
      info.append("Device Available: ").append(isDeviceAvailable()).append("\n");

      if (config != null) {
        info.append("Auto-reconnect: ").append(config.isAutoReconnect()).append("\n");
        info.append("Max retry attempts: ").append(config.getMaxRetryAttempts()).append("\n");
        info.append("Retry delay: ").append(config.getRetryDelayMs()).append("ms\n");
        info.append("Connection timeout: ").append(config.getConnectionTimeoutMs()).append("ms\n");
      }

      return info.toString();
    }
  }

  /**
   * Attempt to recover from error state
   */
  public boolean recoverFromError() {
    synchronized (Lock) {
      if (connectionState != ConnectionState.ERROR) {
        Timber.d("Not in error state, no recovery needed");
        return true;
      }

      Timber.d("Attempting to recover from error state...");

      // Reset state
      connectionState = ConnectionState.DISCONNECTED;

      // Refresh device state
      refreshDeviceState();

      // Try to reconnect
      return reconnect();
    }
  }

  /**
   * Get a cached serial config if available, or attempt to create new one
   */
  @Nullable
  public SerialConfig getCachedOrNewSerialConfig() {
    synchronized (Lock) {
      if (isConnected()) {
        // Try to reuse existing connection
        try {
          SerialConfig serialConfig = new SerialConfig();
          serialConfig.setFileDescriptor(connection.GetFileDescriptor());
          serialConfig.setReadEndpoint(connection.GetReadEndpointAddress());
          serialConfig.setWriteEndpoint(connection.GetWriteEndpointAddress());
          return serialConfig;
        }
        catch (Exception e) {
          Timber.w(e, "Failed to reuse existing connection, creating new one");
        }
      }

      return getSerialConfig();
    }
  }

  // Connection state enum
  public enum ConnectionState {
    DISCONNECTED, CONNECTING, CONNECTED, ERROR
  }

  // Connection listener interface
  public interface ConnectionListener {
    void onConnected();

    void onDisconnected();

    void onError(String error);
  }

  // Configuration class
  public static class Config {
    private final Context context;
    private final int connectionTimeoutMs;
    private final boolean autoReconnect;
    private final int maxRetryAttempts;
    private final long retryDelayMs;

    public Config(Context context) {
      this(context, 5000, true, 5, 1000);
    }

    public Config(Context context, int timeoutMs, boolean autoReconnect, int maxRetryAttempts, long retryDelayMs) {
      this.context = requireNonNull(context, "Context cannot be null");
      this.connectionTimeoutMs = timeoutMs;
      this.autoReconnect = autoReconnect;
      this.maxRetryAttempts = maxRetryAttempts;
      this.retryDelayMs = retryDelayMs;
    }

    public Context getContext() { return context; }

    public int getConnectionTimeoutMs() { return connectionTimeoutMs; }

    public boolean isAutoReconnect() { return autoReconnect; }

    public int getMaxRetryAttempts() { return maxRetryAttempts; }

    public long getRetryDelayMs() { return retryDelayMs; }
  }
}
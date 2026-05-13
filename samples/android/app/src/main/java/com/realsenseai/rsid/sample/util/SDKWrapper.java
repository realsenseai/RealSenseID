// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util;

import static java.util.Objects.requireNonNull;

import android.content.Context;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.realsenseai.rsid.api.DeviceController;
import com.realsenseai.rsid.api.DeviceType;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.api.SerialConfig;
import com.realsenseai.rsid.api.Status;
import com.realsenseai.rsid.impl.UsbCdcConnection;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.regex.Pattern;
import timber.log.Timber;

public enum SDKWrapper {
  INSTANCE;

  private enum State { DISCONNECTED, DEVICE_FOUND, READY }

  private final Object stateLock = new Object();
  private final Object uvcLock = new Object();

  private State state = State.DISCONNECTED;
  private UsbCdcConnection connection;
  private boolean cdcOpen = false;
  private boolean uvcOpen = false;
  private CompletableFuture<Void> readyFuture;
  private DeviceType deviceType = DeviceType.Unknown;
  private volatile Context appContext;

  /**
   * Initialize with the application context. Call once from {@code Application.onCreate}.
   * Idempotent — safe to call multiple times with the same context.
   */
  public void init(@NonNull Context context) {
    requireNonNull(context, "context");
    this.appContext = context.getApplicationContext();
  }

  // ===== Public API =====

  @NonNull
  public DeviceType getDeviceType() {
    return deviceType;
  }

  public void setDeviceType(@NonNull DeviceType type) {
    deviceType = requireNonNull(type, "deviceType");
  }

  /**
   * Drive permission-granted on the device, then run {@code onGranted}.
   * Non-blocking; the runnable fires on the broadcast-receiver thread (or inline if already granted).
   * On denial or no-device, the runnable is not invoked and the failure is logged.
   *
   * Uses a standalone {@link UsbCdcConnection} so this flow is unaffected by {@link #closeConnection()},
   * which the host activity may trigger transiently during the permission dialog (the dialog runs as
   * a separate activity overlay → fragment goes through onPause → onPause closes the SDK connection).
   * Permission is granted system-wide, so a subsequent {@link #getSerialConfig} call will pick it up.
   */
  public void ensurePermissionThenRun(@NonNull Context context, @NonNull Runnable onGranted) {
    requireNonNull(context, "context");
    requireNonNull(onGranted, "onGranted");
    UsbCdcConnection cnx = new UsbCdcConnection();
    if (!cnx.FindSupportedDevice(context)) {
      Timber.w("ensurePermissionThenRun: no supported device");
      return;
    }
    if (cnx.hasPermissionForDevice(context)) {
      runSafely(onGranted);
      return;
    }
    cnx.RequestDevicePermission(context, granted -> {
      if (granted) {
        runSafely(onGranted);
        return;
      }
      // The dialog returned "deny", but if the AndroidManifest declares a <usb-device> filter
      // that matches the attached device, Android grants implicit permission at attach time —
      // and that grant survives the user cancelling the explicit dialog. In that case the
      // device is still openable, so we should run onGranted; otherwise the settings/compat
      // pipeline (which depends on this callback firing) never recovers for the rest of the
      // session, even though the device works fine elsewhere in the app.
      if (cnx.hasPermissionForDevice(context)) {
        Timber.d("ensurePermissionThenRun: dialog denied but device accessible via implicit grant — proceeding");
        runSafely(onGranted);
      }
      else {
        Timber.w("ensurePermissionThenRun: permission denied");
      }
    });
  }

  private static void runSafely(@NonNull Runnable r) {
    try {
      r.run();
    }
    catch (Exception e) {
      Timber.e(e, "ensurePermissionThenRun: onGranted threw");
    }
  }

  /**
   * Returns a freshly-constructed and Connected {@link FaceAuthenticator}, or {@code null} if the
   * device is unavailable or permission was denied. Caller owns the lifetime: {@code Disconnect()}
   * + {@code delete()} when done.
   */
  @Nullable
  public FaceAuthenticator getAuthenticator() {
    SerialConfig cfg = getSerialConfig();
    if (cfg == null) return null;
    FaceAuthenticator auth = new FaceAuthenticator(deviceType);
    Status status = auth.Connect(cfg);
    if (status != Status.Ok) {
      Timber.e("FaceAuthenticator.Connect failed: %s", status);
      auth.delete();
      return null;
    }
    return auth;
  }

  /**
   * Returns a freshly-constructed and Connected {@link DeviceController}, or {@code null} if the
   * device is unavailable or permission was denied. Caller owns the lifetime.
   */
  @Nullable
  public DeviceController getDeviceController() {
    SerialConfig cfg = getSerialConfig();
    if (cfg == null) return null;
    DeviceController controller = new DeviceController(deviceType);
    Status status = controller.Connect(cfg);
    if (status != Status.Ok) {
      Timber.e("DeviceController.Connect failed: %s", status);
      controller.delete();
      return null;
    }
    return controller;
  }

  /**
   * Drives the state machine to READY and ensures the CDC pipe is open, then returns a SerialConfig
   * pointing at the live fd. Returns {@code null} if the device is unavailable or permission denied.
   */
  @Nullable
  public SerialConfig getSerialConfig() {
    if (!ensureReady()) return null;
    synchronized (stateLock) {
      if (!ensureCdcOpenLocked()) return null;
      SerialConfig cfg = new SerialConfig();
      cfg.setFileDescriptor(connection.GetFileDescriptor());
      cfg.setReadEndpoint(connection.GetReadEndpointAddress());
      cfg.setWriteEndpoint(connection.GetWriteEndpointAddress());
      return cfg;
    }
  }

  /** Alias kept for source compatibility. CDC is cached across calls now. */
  @Nullable
  public SerialConfig getCachedOrNewSerialConfig() {
    return getSerialConfig();
  }

  public boolean openUVCConnection() {
    if (!ensureReady()) {
      Timber.w("openUVCConnection: not ready");
      return false;
    }
    synchronized (uvcLock) {
      if (uvcOpen) return true;
      synchronized (stateLock) {
        if (connection == null) return false;
        if (!connection.OpenUvcConnection()) return false;
      }
      uvcOpen = true;
      return true;
    }
  }

  public int getUVCFileDescriptor() {
    synchronized (uvcLock) {
      synchronized (stateLock) {
        return connection == null ? -1 : connection.GetUvcFileDescriptor();
      }
    }
  }

  public void closeUVCConnection() {
    synchronized (uvcLock) {
      synchronized (stateLock) {
        if (uvcOpen && connection != null) {
          try {
            connection.CloseUvcConnection();
          }
          catch (Exception e) {
            Timber.w(e, "CloseUvcConnection threw");
          }
        }
        uvcOpen = false;
      }
    }
  }

  /**
   * Tear down the CDC pipe. State returns to DISCONNECTED; permission persists at the system level.
   *
   * Does NOT close UVC — UVC has an independent lifecycle. Some call sites (notably
   * {@code BaseRealSenseIdPreviewFragment.startPreview}'s finally block) call closeConnection
   * immediately after handing the UVC fd to the native preview pipeline; closing UVC here would
   * pull the fd out from under libuvc mid-startup. UVC is closed explicitly by callers via
   * {@link #closeUVCConnection()}.
   */
  public void closeConnection() {
    synchronized (stateLock) {
      if (cdcOpen && connection != null) {
        try {
          connection.CloseConnection();
        }
        catch (Exception e) {
          Timber.w(e, "CloseConnection threw");
        }
      }
      cdcOpen = false;
      connection = null;
      state = State.DISCONNECTED;
      if (readyFuture != null && !readyFuture.isDone()) {
        readyFuture.cancel(true);
      }
      readyFuture = null;
    }
  }

  public int getSKUVersion() {
    DeviceController controller = getDeviceController();
    if (controller == null) {
      throw new RuntimeException("Unable to obtain device controller handle");
    }
    try {
      short[] asciiArr = new short[1];
      Status status = controller.QueryOtpVersion(asciiArr);
      if (status == Status.Ok) {
        int sku = (int) asciiArr[0] - (int) '0' + 1;
        Timber.i("Using OTP: deviceSkuVer = %d", sku);
        return sku;
      }
      String[] snArr = new String[1];
      status = controller.QuerySerialNumber(snArr);
      if (status == Status.Ok) {
        String sn = snArr[0];
        Pattern reg1 = Pattern.compile("12[02]\\d6228\\d{4}.*");
        Pattern reg2 = Pattern.compile("\\d{4}6229\\d{4}.*");
        Pattern reg3 = Pattern.compile("\\d{4}62[3-9]\\d{5}.*");
        if (reg1.matcher(sn).matches() || reg2.matcher(sn).matches() || reg3.matcher(sn).matches()) {
          return 2;
        }
        return 1;
      }
      throw new RuntimeException("Unable to get SKU version (OTP and SN both failed)");
    }
    finally {
      try {
        controller.Disconnect();
      }
      catch (Exception e) {
        Timber.w(e, "DeviceController.Disconnect threw");
      }
      try {
        controller.delete();
      }
      catch (Exception e) {
        Timber.w(e, "DeviceController.delete threw");
      }
    }
  }

  @NonNull
  public String getDiagnosticInfo() {
    StringBuilder info = new StringBuilder("SDKWrapper:\n");
    synchronized (stateLock) {
      info.append("  state: ").append(state).append('\n');
      info.append("  cdcOpen: ").append(cdcOpen).append('\n');
      info.append("  deviceType: ").append(deviceType).append('\n');
    }
    synchronized (uvcLock) {
      info.append("  uvcOpen: ").append(uvcOpen).append('\n');
    }
    return info.toString();
  }

  // ===== State machine =====

  /** Synchronous wrapper. Returns true once state is READY; false on no-device or denial. */
  private boolean ensureReady() {
    try {
      ensureReadyAsync().get();
      return true;
    }
    catch (InterruptedException e) {
      Thread.currentThread().interrupt();
      Timber.w("ensureReady interrupted");
      return false;
    }
    catch (ExecutionException e) {
      Timber.w(e.getCause(), "ensureReady failed");
      return false;
    }
  }

  /**
   * Drives DISCONNECTED → DEVICE_FOUND → READY. Concurrent callers share the same in-flight future.
   * Completes exceptionally on no-device or permission denial; the caller decides whether to retry.
   */
  private CompletableFuture<Void> ensureReadyAsync() {
    Context ctx = appContext();
    CompletableFuture<Void> waitOn;
    synchronized (stateLock) {
      if (state == State.READY) {
        return CompletableFuture.completedFuture(null);
      }

      if (state == State.DISCONNECTED) {
        if (connection == null) connection = new UsbCdcConnection();
        if (!connection.FindSupportedDevice(ctx)) {
          return failedFuture(new RuntimeException("No supported RealSense device found"));
        }
        state = State.DEVICE_FOUND;
      }

      // state == DEVICE_FOUND
      // Defensive: state may be DEVICE_FOUND while connection is null if some path nulled the
      // reference without resetting state back to DISCONNECTED. Re-discover so the next
      // dereferences below are safe.
      if (connection == null) {
        connection = new UsbCdcConnection();
        if (!connection.FindSupportedDevice(ctx)) {
          return failedFuture(new RuntimeException("No supported RealSense device found"));
        }
        state = State.DEVICE_FOUND;
      }
      if (connection.hasPermissionForDevice(ctx)) {
        state = State.READY;
        return CompletableFuture.completedFuture(null);
      }

      if (readyFuture == null) {
        readyFuture = new CompletableFuture<>();
        // Callback may fire synchronously (fast path: hasPermission already true) or async (broadcast).
        // RequestDevicePermission already guards against double-callback and has its own 15s timeout.
        connection.RequestDevicePermission(ctx, this::onPermissionResult);
      }
      waitOn = readyFuture;
    }
    return waitOn;
  }

  private void onPermissionResult(boolean granted) {
    CompletableFuture<Void> toComplete;
    synchronized (stateLock) {
      toComplete = readyFuture;
      readyFuture = null;
      if (granted) state = State.READY;
      // Denial leaves state at DEVICE_FOUND; next ensureReady call will request again.
    }
    if (toComplete == null) return;
    if (granted) {
      toComplete.complete(null);
    }
    else {
      toComplete.completeExceptionally(new RuntimeException("USB permission denied"));
    }
  }

  /** Caller must hold {@link #stateLock}. */
  private boolean ensureCdcOpenLocked() {
    if (cdcOpen) return true;
    if (connection == null || state != State.READY) {
      Timber.e("ensureCdcOpen: not READY (state=%s)", state);
      return false;
    }
    // OpenConnection can fail transiently right after a previous closeConnection: the kernel
    // hasn't released the CDC interface claim yet, or the device's USB stack is still settling.
    // One short retry covers this race; persistent failure indicates a real problem.
    if (!connection.OpenConnection()) {
      Timber.w("OpenConnection failed; retrying after short delay");
      try {
        Thread.sleep(150);
      }
      catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        return false;
      }
      if (!connection.OpenConnection()) {
        Timber.e("OpenConnection failed after retry");
        return false;
      }
    }
    cdcOpen = true;
    return true;
  }

  // ===== Internals =====

  @NonNull
  private Context appContext() {
    Context ctx = appContext;
    if (ctx == null) {
      throw new IllegalStateException(
        "SDKWrapper not initialized. Call SDKWrapper.INSTANCE.init(context) in Application.onCreate.");
    }
    return ctx;
  }

  private static <T> CompletableFuture<T> failedFuture(Throwable t) {
    CompletableFuture<T> f = new CompletableFuture<>();
    f.completeExceptionally(t);
    return f;
  }
}

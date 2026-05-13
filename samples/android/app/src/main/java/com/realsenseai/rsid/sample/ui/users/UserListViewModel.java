// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.users;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.sample.MainApplication;
import com.realsenseai.rsid.sample.db.AppDatabase;
import com.realsenseai.rsid.sample.db.User;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import timber.log.Timber;

/**
 * Activity-scoped so the device-users list survives navigation between fragments. The cache is
 * invalidated on (a) successful enrollment from the preview screen, (b) user delete from this
 * screen, or (c) USB attach/detach — otherwise the cached list is reused on each visit.
 *
 * <p>The {@link MutableLiveData} fields are themselves the cache; {@link #deviceUsersStale} is
 * just the freshness flag.
 */
public class UserListViewModel extends ViewModel {

  private static final int MAX_USER_ID_LENGTH = 31; // 30 + null terminator
  private static final int MAX_REASONABLE_USER_COUNT = 5000;

  private final MutableLiveData<List<DeviceUser>> deviceUsers = new MutableLiveData<>();
  private final MutableLiveData<List<User>> hostUsers = new MutableLiveData<>();
  private final MutableLiveData<Boolean> isLoading = new MutableLiveData<>(false);
  private final MutableLiveData<String> error = new MutableLiveData<>();
  private final MutableLiveData<DeletionResult> deletionResult = new MutableLiveData<>();

  private final ExecutorService executor = Executors.newSingleThreadExecutor();
  private final AppDatabase database;

  // In-flight device-load task; cancelled on fragment view destroy.
  private volatile Future<?> currentDeviceLoad;
  private volatile boolean cancelled;

  // The authenticator currently servicing a device-load. Held under {@link #authLock} so
  // {@link #cancelDeviceLoad()} can signal the device-side abort before the executor disposes it.
  private final Object authLock = new Object();
  @Nullable
  private FaceAuthenticator inFlightAuth;

  // Stale on first construction; cleared by a successful device load.
  private volatile boolean deviceUsersStale = true;

  public UserListViewModel() {
    database = AppDatabase.getInstance(MainApplication.getInstance());
  }

  // ===== Device users =====

  @NonNull
  public LiveData<List<DeviceUser>> getDeviceUsers() {
    return deviceUsers;
  }

  /**
   * Mark the cache stale so the next {@link #loadDeviceUsersIfNeeded()} reads from the device.
   */
  public void markDeviceUsersStale() {
    deviceUsersStale = true;
  }

  /**
   * Load device users only when the cache is stale or empty. Otherwise the existing
   * {@link #getDeviceUsers()} value is replayed to new observers automatically.
   */
  public void loadDeviceUsersIfNeeded() {
    if (!deviceUsersStale && deviceUsers.getValue() != null) {
      return;
    }
    cancelled = false;
    isLoading.postValue(true);
    currentDeviceLoad = executor.submit(this::runDeviceLoad);
  }

  /**
   * Cancel the in-flight device load. Sets the cancellation flag so post-USB checks bail, signals
   * the device to abort its current operation via {@code Cancel()}, then closes the CDC connection
   * so any blocking USB ioctl returns with EBADF (Java thread interrupt doesn't propagate into JNI).
   */
  public void cancelDeviceLoad() {
    Future<?> f = currentDeviceLoad;
    if (f == null || f.isDone()) {
      // Nothing in flight — don't touch the shared SDKWrapper. Calling closeConnection() here
      // when the load already completed cleanly would yank the cached CDC fd out from under
      // any other consumer (e.g. SharedViewModel's loadDeviceSettings running on its own thread).
      return;
    }
    cancelled = true;
    f.cancel(true);
    synchronized (authLock) {
      if (inFlightAuth != null) {
        try {
          inFlightAuth.Cancel();
        }
        catch (Exception e) {
          Timber.w(e, "auth.Cancel");
        }
      }
    }
    // Hard-interrupt: closing the fd makes the kernel return EBADF to any blocking USB ioctl
    // the JNI thread is stuck on (Java thread interrupt doesn't propagate into JNI). Only safe
    // because we just confirmed there's an in-flight load that owns this connection.
    SDKWrapper.INSTANCE.closeConnection();
  }

  public void deleteDeviceDbUser(int position) {
    List<DeviceUser> current = deviceUsers.getValue();
    if (current == null || position < 0 || position >= current.size()) {
      deletionResult.postValue(new DeletionResult(false, "Invalid user position"));
      return;
    }
    DeviceUser toDelete = current.get(position);
    executor.execute(() -> runDeviceDelete(current, position, toDelete));
  }

  private void runDeviceLoad() {
    FaceAuthenticator auth = null;
    try {
      if (cancelled) return;

      auth = SDKWrapper.INSTANCE.getAuthenticator();
      if (auth == null || cancelled) return;
      synchronized (authLock) {
        inFlightAuth = auth;
      }

      long[] count = {0};
      auth.QueryNumberOfUsers(count);
      if (cancelled) return;

      if (count[0] <= 0 || count[0] > MAX_REASONABLE_USER_COUNT) {
        Timber.d("Device user count out of range: %d (treating as empty)", count[0]);
        deviceUsers.postValue(Collections.emptyList());
        deviceUsersStale = false;
        return;
      }

      String[] ids = new String[(int)count[0]];
      for (int i = 0; i < ids.length; i++) {
        ids[i] = new String(new char[MAX_USER_ID_LENGTH]);
      }
      auth.QueryUserIds(ids, count);
      if (cancelled) return;

      List<DeviceUser> list = new ArrayList<>(ids.length);
      for (String id : ids) {
        list.add(new DeviceUser(id));
      }
      deviceUsers.postValue(list);
      deviceUsersStale = false;
      Timber.d("Loaded %d device users", list.size());
    }
    catch (Exception e) {
      if (cancelled) {
        Timber.d("Device user load cancelled: %s", e.getMessage());
      }
      else {
        Timber.e(e, "Failed to load device users");
        error.postValue("Failed to load users: " + e.getMessage());
      }
    }
    finally {
      synchronized (authLock) {
        inFlightAuth = null;
      }
      disposeAuthenticator(auth);
      isLoading.postValue(false);
    }
  }

  private void runDeviceDelete(@NonNull List<DeviceUser> snapshot, int position, @NonNull DeviceUser toDelete) {
    FaceAuthenticator auth = null;
    try {
      auth = SDKWrapper.INSTANCE.getAuthenticator();
      if (auth == null) {
        deletionResult.postValue(new DeletionResult(false, "Device not connected"));
        return;
      }
      auth.RemoveUser(toDelete.getName());

      // Force a fresh read on the next visit; reflect the deletion locally for current-screen UI.
      deviceUsersStale = true;
      List<DeviceUser> updated = new ArrayList<>(snapshot);
      updated.remove(position);
      deviceUsers.postValue(updated);

      deletionResult.postValue(new DeletionResult(true, null));
      Timber.d("Deleted device user: %s", toDelete.getName());
    }
    catch (Exception e) {
      Timber.e(e, "Failed to delete device user: %s", toDelete.getName());
      deletionResult.postValue(new DeletionResult(false, e.getMessage()));
    }
    finally {
      disposeAuthenticator(auth);
    }
  }

  // ===== Host users =====

  @NonNull
  public LiveData<List<User>> getHostUsers() {
    return hostUsers;
  }

  public void loadHostDbUsers() {
    isLoading.postValue(true);
    executor.execute(() -> {
      try {
        List<User> list = database.userDao().getAll();
        hostUsers.postValue(list);
        Timber.d("Loaded %d host users", list.size());
      }
      catch (Exception e) {
        Timber.e(e, "Failed to load host users");
        error.postValue("Failed to load users: " + e.getMessage());
      }
      finally {
        isLoading.postValue(false);
      }
    });
  }

  public void deleteHostDbUser(int position) {
    List<User> current = hostUsers.getValue();
    if (current == null || position < 0 || position >= current.size()) {
      deletionResult.postValue(new DeletionResult(false, "Invalid user position"));
      return;
    }
    User toDelete = current.get(position);
    executor.execute(() -> {
      try {
        database.userDao().delete(toDelete);
        List<User> updated = new ArrayList<>(current);
        updated.remove(position);
        hostUsers.postValue(updated);
        deletionResult.postValue(new DeletionResult(true, null));
        Timber.d("Deleted host user: %s", toDelete.name);
      }
      catch (Exception e) {
        Timber.e(e, "Failed to delete host user: %s", toDelete.name);
        deletionResult.postValue(new DeletionResult(false, e.getMessage()));
      }
    });
  }

  // ===== Common =====

  @NonNull
  public LiveData<Boolean> getIsLoading() {
    return isLoading;
  }

  @NonNull
  public LiveData<String> getError() {
    return error;
  }

  @NonNull
  public LiveData<DeletionResult> getDeletionResult() {
    return deletionResult;
  }

  public void clearError() {
    error.setValue(null);
  }

  public void clearDeletionResult() {
    deletionResult.setValue(null);
  }

  @Override
  protected void onCleared() {
    super.onCleared();
    executor.shutdown();
  }

  private static void disposeAuthenticator(@Nullable FaceAuthenticator auth) {
    if (auth == null) return;
    try {
      auth.Disconnect();
    }
    catch (Exception e) {
      Timber.w(e, "auth.Disconnect");
    }
    try {
      auth.delete();
    }
    catch (Exception e) {
      Timber.w(e, "auth.delete");
    }
  }

  public record DeletionResult(boolean success, @Nullable String error) {
  }
}

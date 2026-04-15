// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.users;

import static java.util.Objects.requireNonNull;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.sample.MainApplication;
import com.realsenseai.rsid.sample.db.AppDatabase;
import com.realsenseai.rsid.sample.db.User;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import timber.log.Timber;

public class UserListViewModel<T> extends ViewModel {

  // LiveData for UI state
  private final MutableLiveData<List<T>> users = new MutableLiveData<>();
  private final MutableLiveData<Boolean> isLoading = new MutableLiveData<>(false);
  private final MutableLiveData<String> error = new MutableLiveData<>();
  private final MutableLiveData<DeletionResult> deletionResult = new MutableLiveData<>();

  // Background executor
  private final ExecutorService executor = Executors.newSingleThreadExecutor();

  // Database reference
  private final AppDatabase database;

  public UserListViewModel() {
    // Initialize database
    database = AppDatabase.getInstance(MainApplication.getInstance());
  }

  // Public API
  public LiveData<List<T>> getUsers() {
    return users;
  }

  public void clearUsers() {
    users.postValue(new ArrayList<>());
  }

  public LiveData<Boolean> getIsLoading() {
    return isLoading;
  }

  public LiveData<String> getError() {
    return error;
  }

  public LiveData<DeletionResult> getDeletionResult() {
    return deletionResult;
  }

  public void loadDeviceDbUsers() {
    isLoading.postValue(true);

    SDKWrapper.INSTANCE.closeConnection();

    executor.execute(() -> {
      FaceAuthenticator authenticator = null;
      try {
        users.postValue(new ArrayList<>());

        long[] userIdsLong = {0};

        authenticator = requireNonNull(SDKWrapper.INSTANCE.getAuthenticator());
        // TODO: The following is a work-around for when coming back from
        //       The firmware screen.
        authenticator.Disconnect();
        authenticator.Connect(SDKWrapper.INSTANCE.getCachedOrNewSerialConfig());

        authenticator.QueryNumberOfUsers(userIdsLong);
        if (userIdsLong[0] <= 0 || userIdsLong[0] > 5000) { // Add reasonable upper bound
          users.postValue(new ArrayList<>());
          Timber.d("Invalid user count: %d", userIdsLong[0]);
          return;
        }

        String[] userIds = new String[(int)userIdsLong[0]];
        for (int i = 0; i < userIdsLong[0]; i++) {
          userIds[i] = new String(new char[31]); // MAX_USERID_LENGTH = 30 + null terminator
        }

        authenticator.QueryUserIds(userIds, userIdsLong);
        List<DeviceUser> userList = new ArrayList<>();
        for (int i = 0; i < userIdsLong[0]; i++) {
          userList.add(new DeviceUser(userIds[i]));
        }
        users.postValue((List<T>)userList);
        Timber.d("Loaded %d users", userList.size());
      }
      catch (Exception e) {
        Timber.e(e, "Failed to load users");
        error.postValue("Failed to load users: " + e.getMessage());
      }
      finally {
        if (authenticator != null) {
          authenticator.Disconnect();
          authenticator.delete();
        }
        SDKWrapper.INSTANCE.closeConnection();

        isLoading.postValue(false);
      }
    });
  }

  public void deleteDeviceDbUser(int position) {
    var currentUsers = users.getValue();
    if (currentUsers == null || position < 0 || position >= currentUsers.size()) {
      deletionResult.postValue(new DeletionResult(false, "Invalid user position"));
      return;
    }

    var userToDelete = currentUsers.get(position);


    executor.execute(() -> {
      FaceAuthenticator authenticator = null;
      try {
        authenticator = requireNonNull(SDKWrapper.INSTANCE.getAuthenticator());
        authenticator.RemoveUser(((DeviceUser)userToDelete).getName());

        // Update the list by removing the deleted user
        var updatedUsers = users.getValue();
        if (updatedUsers != null) {
          updatedUsers.remove(position);
          users.postValue(updatedUsers);
        }

        deletionResult.postValue(new DeletionResult(true, null));
        Timber.d("Deleted user: %s", ((DeviceUser)userToDelete).getName());

        SDKWrapper.INSTANCE.closeConnection();
      }
      catch (Exception e) {
        Timber.e(e, "Failed to delete user: %s", ((DeviceUser)userToDelete).getName());
        deletionResult.postValue(new DeletionResult(false, e.getMessage()));
      }
      finally {
        if (authenticator != null) {
          authenticator.Disconnect();
          authenticator.delete();
        }
      }
    });
  }


  public void loadHostDbUsers() {
    isLoading.postValue(true);
    executor.execute(() -> {
      try {
        users.postValue(new ArrayList<>());

        var userList = database.userDao().getAll();
        users.postValue((List<T>)userList);
        Timber.d("Loaded %d users", userList.size());
      }
      catch (Exception e) {
        Timber.e(e, "Failed to load users");
        error.postValue("Failed to load users: " + e.getMessage());
      }
      finally {
        isLoading.postValue(false);
      }
    });
  }

  public void deleteHostDbUser(int position) {
    var currentUsers = users.getValue();
    if (currentUsers == null || position < 0 || position >= currentUsers.size()) {
      deletionResult.postValue(new DeletionResult(false, "Invalid user position"));
      return;
    }

    var userToDelete = currentUsers.get(position);
    executor.execute(() -> {
      try {
        database.userDao().delete((User)userToDelete);

        // Update the list by removing the deleted user
        var updatedUsers = users.getValue();
        if (updatedUsers != null) {
          updatedUsers.remove(position);
          users.postValue(updatedUsers);
        }

        deletionResult.postValue(new DeletionResult(true, null));
        Timber.d("Deleted user: %s", ((User)userToDelete).name);
      }
      catch (Exception e) {
        Timber.e(e, "Failed to delete user: %s", ((User)userToDelete).name);
        deletionResult.postValue(new DeletionResult(false, e.getMessage()));
      }
    });
  }

  public void refreshUsers() {
    loadHostDbUsers();
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
    Timber.d("UserListViewModel cleared");
  }

  // Result class for deletion operations
  public static class DeletionResult {
    private final boolean success;
    private final String error;

    public DeletionResult(boolean success, String error) {
      this.success = success;
      this.error = error;
    }

    public boolean isSuccess() {
      return success;
    }

    public String getError() {
      return error;
    }
  }
}
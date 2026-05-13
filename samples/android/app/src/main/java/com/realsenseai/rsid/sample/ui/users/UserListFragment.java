// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.users;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import androidx.recyclerview.widget.DividerItemDecoration;
import androidx.recyclerview.widget.LinearLayoutManager;
import com.google.android.material.snackbar.Snackbar;
import com.realsenseai.rsid.sample.databinding.FragmentUsersBinding;
import com.realsenseai.rsid.sample.db.User;
import com.realsenseai.rsid.sample.ui.shared.RealSenseIdSharedViewModel;

public class UserListFragment extends Fragment {

  private FragmentUsersBinding binding;
  private UserListAdapter<User> hostDbAdapter;
  private UserListAdapter<DeviceUser> deviceDbAdapter;
  private UserListViewModel userListViewModel;
  private boolean useHostDb = false;

  @Nullable
  @Override
  public View onCreateView(@NonNull LayoutInflater inflater,
                           @Nullable ViewGroup container,
                           @Nullable Bundle savedInstanceState) {
    binding = FragmentUsersBinding.inflate(inflater, container, false);

    var sharedViewModel = new ViewModelProvider(requireActivity()).get(RealSenseIdSharedViewModel.class);
    // Activity-scoped so the device-users LiveData survives across navigation.
    userListViewModel = new ViewModelProvider(requireActivity()).get(UserListViewModel.class);
    useHostDb = Boolean.TRUE.equals(sharedViewModel.getUseHostDb().getValue());

    if (useHostDb) {
      hostDbAdapter =
        new UserListAdapter<>(position -> userListViewModel.deleteHostDbUser(position), UserListAdapter.getDbUserItemBinder());
      binding.recyclerviewUsers.setAdapter(hostDbAdapter);
    }
    else {
      deviceDbAdapter =
        new UserListAdapter<>(position -> userListViewModel.deleteDeviceDbUser(position), UserListAdapter.getDeviceUserItemBinder());
      binding.recyclerviewUsers.setAdapter(deviceDbAdapter);
    }

    setupRecyclerView();
    observeViewModel();

    if (useHostDb) {
      userListViewModel.loadHostDbUsers();
    }
    else {
      userListViewModel.loadDeviceUsersIfNeeded();
    }

    return binding.getRoot();
  }

  private void setupRecyclerView() {
    binding.recyclerviewUsers.setLayoutManager(new LinearLayoutManager(requireContext()));
    binding.recyclerviewUsers.addItemDecoration(new DividerItemDecoration(requireContext(), DividerItemDecoration.VERTICAL));
  }

  private void observeViewModel() {
    if (useHostDb) {
      userListViewModel.getHostUsers().observe(getViewLifecycleOwner(), users -> {
        if (users != null) hostDbAdapter.updateUsers(users);
      });
    }
    else {
      userListViewModel.getDeviceUsers().observe(getViewLifecycleOwner(), users -> {
        if (users != null) deviceDbAdapter.updateUsers(users);
      });
    }

    userListViewModel.getIsLoading().observe(getViewLifecycleOwner(), isLoading -> {
      // binding.progressBar.setVisibility(isLoading ? View.VISIBLE : View.GONE);
    });

    userListViewModel.getError().observe(getViewLifecycleOwner(), error -> {
      if (error != null && !error.isEmpty()) {
        showError(error);
        userListViewModel.clearError();
      }
    });

    userListViewModel.getDeletionResult().observe(getViewLifecycleOwner(), result -> {
      if (result != null) {
        if (result.success()) {
          showMessage("User deleted successfully");
        }
        else {
          showError("Failed to delete user: " + result.error());
        }
        userListViewModel.clearDeletionResult();
      }
    });
  }

  private void showError(@NonNull String message) {
    if (binding != null) {
      Snackbar.make(binding.getRoot(), message, Snackbar.LENGTH_LONG).show();
    }
  }

  private void showMessage(@NonNull String message) {
    if (binding != null) {
      Snackbar.make(binding.getRoot(), message, Snackbar.LENGTH_SHORT).show();
    }
  }

  @Override
  public void onDestroyView() {
    super.onDestroyView();
    if (!useHostDb) {
      userListViewModel.cancelDeviceLoad();
    }
    binding = null;
    hostDbAdapter = null;
    deviceDbAdapter = null;
  }
}

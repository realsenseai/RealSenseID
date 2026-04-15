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
import java.util.List;

public class UserListFragment extends Fragment {

  private FragmentUsersBinding binding;
  private UserListAdapter<User> hostDbAdapter;
  private UserListAdapter<DeviceUser> deviceDbAdapter;
  private UserListViewModel<?> userListViewModel;
  private boolean useHostDb = false;

  @Nullable
  @Override
  public View onCreateView(@NonNull LayoutInflater inflater,
                           @Nullable ViewGroup container,
                           @Nullable Bundle savedInstanceState) {
    userListViewModel = new ViewModelProvider(this).get(UserListViewModel.class);
    userListViewModel.clearUsers();

    binding = FragmentUsersBinding.inflate(inflater, container, false);

    var sharedViewModel = new ViewModelProvider(requireActivity()).get(RealSenseIdSharedViewModel.class);
    useHostDb = sharedViewModel.getUseHostDb().getValue() != null && sharedViewModel.getUseHostDb().getValue() == true;

    if (useHostDb) {
      hostDbAdapter =
        new UserListAdapter<>(position -> userListViewModel.deleteHostDbUser(position), UserListAdapter.getDbUserItemBinder());
      var recyclerView = binding.recyclerviewUsers;
      recyclerView.setAdapter(hostDbAdapter);
    }
    else {
      deviceDbAdapter =
        new UserListAdapter<>(position -> userListViewModel.deleteDeviceDbUser(position), UserListAdapter.getDeviceUserItemBinder());
      var recyclerView = binding.recyclerviewUsers;
      recyclerView.setAdapter(deviceDbAdapter);
    }

    setupRecyclerView();
    observeViewModel();

    // Load users when fragment is created
    if (useHostDb) {
      userListViewModel.loadHostDbUsers();
    }
    else {
      userListViewModel.loadDeviceDbUsers();
    }

    return binding.getRoot();
  }

  private void setupRecyclerView() {
    var recyclerView = binding.recyclerviewUsers;

    recyclerView.setLayoutManager(new LinearLayoutManager(requireContext()));

    // Add divider decoration
    var divider = new DividerItemDecoration(requireContext(), DividerItemDecoration.VERTICAL);
    recyclerView.addItemDecoration(divider);
  }


  private void observeViewModel() {
    // Observe users list
    userListViewModel.getUsers().observe(getViewLifecycleOwner(), users -> {
      if (users != null) {
        if (useHostDb) {
          hostDbAdapter.updateUsers((List<User>)users);
        }
        else {
          deviceDbAdapter.updateUsers((List<DeviceUser>)users);
        }
      }
    });

    // Observe loading state
    userListViewModel.getIsLoading().observe(getViewLifecycleOwner(), isLoading -> {
      // binding.progressBar.setVisibility(isLoading ? View.VISIBLE : View.GONE);
    });

    // Observe errors
    userListViewModel.getError().observe(getViewLifecycleOwner(), error -> {
      if (error != null && !error.isEmpty()) {
        showError(error);
        userListViewModel.clearError(); // Clear error after showing
      }
    });

    // Observe deletion results
    userListViewModel.getDeletionResult().observe(getViewLifecycleOwner(), result -> {
      if (result != null) {
        if (result.isSuccess()) {
          showMessage("User deleted successfully");
        }
        else {
          showError("Failed to delete user: " + result.getError());
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
    binding = null;
    hostDbAdapter = null;
    deviceDbAdapter = null;
    userListViewModel.clearUsers();
  }
}
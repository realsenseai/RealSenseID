// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.users;

import android.view.LayoutInflater;
import android.view.ViewGroup;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.recyclerview.widget.DiffUtil;
import androidx.recyclerview.widget.RecyclerView;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.realsenseai.rsid.sample.databinding.ItemUserBinding;
import com.realsenseai.rsid.sample.db.User;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import timber.log.Timber;

public class UserListAdapter<T> extends RecyclerView.Adapter<UserListAdapter<T>.UserViewHolder> {

  private final List<T> users = new ArrayList<>();
  private final OnUserActionListener<T> listener;
  private final ItemBinder<T> itemBinder;

  public UserListAdapter(@NonNull OnUserActionListener<T> listener, @NonNull ItemBinder<T> itemBinder) {
    this.listener = listener;
    this.itemBinder = itemBinder;
  }

  static public UserListAdapter.ItemBinder<User> getDbUserItemBinder() {
    return new UserListAdapter.ItemBinder<>() {
      @Override
      public void bind(@NonNull TextView textView, @NonNull User user) {
        textView.setText(user.name);
      }

      @NonNull
      @Override
      public String getItemName(@NonNull User user) {
        return user.name;
      }

      @Override
      public long getItemId(@NonNull User user) {
        return user.id;
      }

      @Override
      public boolean areItemsTheSame(@NonNull User oldUser, @NonNull User newUser) {
        return Objects.equals(oldUser.id, newUser.id);
      }

      @Override
      public boolean areContentsTheSame(@NonNull User oldUser, @NonNull User newUser) {
        return oldUser.name.equals(newUser.name) && Objects.equals(oldUser.id, newUser.id);
      }
    };
  }

  static public UserListAdapter.ItemBinder<DeviceUser> getDeviceUserItemBinder() {
    return new UserListAdapter.ItemBinder<>() {
      @Override
      public void bind(@NonNull TextView textView, @NonNull DeviceUser user) {
        textView.setText(user.getName());
      }

      @NonNull
      @Override
      public String getItemName(@NonNull DeviceUser user) {
        return user.getName();
      }

      @Override
      public long getItemId(@NonNull DeviceUser user) {
        return user.getName().hashCode();
      }

      @Override
      public boolean areItemsTheSame(@NonNull DeviceUser oldUser, @NonNull DeviceUser newUser) {
        return Objects.equals(oldUser.getName(), newUser.getName());
      }

      @Override
      public boolean areContentsTheSame(@NonNull DeviceUser oldUser, @NonNull DeviceUser newUser) {
        return oldUser.getName().equals(newUser.getName());
      }
    };
  }

  public void updateUsers(@NonNull List<T> newUsers) {
    // Use DiffUtil for better performance with animations
    var diffCallback = new UserDiffCallback<T>(this.users, newUsers, itemBinder);
    var diffResult = DiffUtil.calculateDiff(diffCallback);

    this.users.clear();
    this.users.addAll(newUsers);
    diffResult.dispatchUpdatesTo(this);
  }

  @NonNull
  @Override
  public UserViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
    var binding = ItemUserBinding.inflate(LayoutInflater.from(parent.getContext()), parent, false);
    return new UserViewHolder(binding);
  }

  @Override
  public void onBindViewHolder(@NonNull UserViewHolder holder, int position) {
    var item = users.get(position);
    try {
      holder.bind(item);
    }
    catch (Exception e) {
      Timber.e(e, "Error binding item at position %d", position);
    }
  }

  @Override
  public int getItemCount() {
    return users.size();
  }

  // Interface for handling user actions
  public interface OnUserActionListener<T> {
    void onDeleteUser(int position);
  }

  // Interface for binding generic items
  public interface ItemBinder<T> {
    void bind(@NonNull TextView textView, @NonNull T item);

    @NonNull
    String getItemName(@NonNull T item);

    long getItemId(@NonNull T item);

    boolean areItemsTheSame(@NonNull T oldItem, @NonNull T newItem);

    boolean areContentsTheSame(@NonNull T oldItem, @NonNull T newItem);
  }

  // DiffUtil callback for efficient list updates
  private static class UserDiffCallback<T> extends DiffUtil.Callback {
    private final List<T> oldList;
    private final List<T> newList;
    private final ItemBinder<T> itemBinder;

    public UserDiffCallback(@NonNull List<T> oldList, @NonNull List<T> newList, @NonNull ItemBinder<T> itemBinder) {
      this.oldList = oldList;
      this.newList = newList;
      this.itemBinder = itemBinder;
    }

    @Override
    public int getOldListSize() {
      return oldList.size();
    }

    @Override
    public int getNewListSize() {
      return newList.size();
    }

    @Override
    public boolean areItemsTheSame(int oldItemPosition, int newItemPosition) {
      return itemBinder.areItemsTheSame(oldList.get(oldItemPosition), newList.get(newItemPosition));
    }

    @Override
    public boolean areContentsTheSame(int oldItemPosition, int newItemPosition) {
      return itemBinder.areContentsTheSame(oldList.get(oldItemPosition), newList.get(newItemPosition));
    }
  }

  public class UserViewHolder extends RecyclerView.ViewHolder {
    private final ItemUserBinding binding;
    private final TextView textView;

    public UserViewHolder(@NonNull ItemUserBinding binding) {
      super(binding.getRoot());
      this.binding = binding;
      this.textView = binding.textViewItemUser;

      setupClickListeners();
    }

    private void setupClickListeners() {
      binding.buttonDelete.setOnClickListener(v -> showDeleteConfirmation());
    }

    public void bind(@NonNull T item) {
      itemBinder.bind(textView, item);
      // Set content description for accessibility
      binding.getRoot().setContentDescription("User: " + itemBinder.getItemName(item));
    }

    private void showDeleteConfirmation() {
      var context = binding.getRoot().getContext();
      var position = getBindingAdapterPosition();

      if (position == RecyclerView.NO_POSITION) {
        return; // Item was removed, ignore click
      }

      var item = users.get(position);

      new MaterialAlertDialogBuilder(context)
        .setTitle("Delete User")
        .setMessage("Are you sure you want to delete \"" + itemBinder.getItemName(item) + "\"?")
        .setPositiveButton("Delete", (dialog, which) -> {
          listener.onDeleteUser(position);
        })
        .setNegativeButton("Cancel", null)
        .setCancelable(true)
        .show();
    }
  }
}
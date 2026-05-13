// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.preview;

import static java.util.Objects.isNull;
import static java.util.Objects.requireNonNull;

import android.content.Context;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.provider.MediaStore;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodManager;
import android.widget.ImageView;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.textfield.TextInputLayout;
import com.realsenseai.rsid.sample.R;
import com.realsenseai.rsid.sample.util.SnackbarHelper;
import java.util.ArrayList;
import java.util.List;
import timber.log.Timber;

public class UIDialogHelper {

  /**
   * Shows an enrollment dialog with text input for entering a user name
   */
  public static void showEnrollmentDialog(@NonNull Context context,
                                          @Nullable View rootView,
                                          @NonNull EnrollmentDialogCallback callback) {
    MaterialAlertDialogBuilder alertName = new MaterialAlertDialogBuilder(context);
    alertName.setTitle("Enter name");
    alertName.setView(R.layout.dialog_text_input);

    alertName.setPositiveButton("OK", (d, whichButton) -> {
      TextInputLayout editTextName = ((AlertDialog)d).findViewById(R.id.dialog_text_input_layout);
      assert editTextName != null;
      String userName = requireNonNull(editTextName.getEditText()).getText().toString();

      if (isNull(userName) || userName.trim().isEmpty()) {
        if (rootView != null) {
          SnackbarHelper.showSnackbarMessage(rootView, "Please enter a name!", context);
        }
      }
      else {
        d.dismiss();
        hideKeyboard(context, editTextName);
        callback.onEnrollmentConfirmed(userName);
      }
    });

    alertName.setNegativeButton("Cancel", (d, whichButton) -> {
      d.cancel();
      callback.onEnrollmentCancelled();
    });

    var dialog = alertName.show();
    dialog.setCanceledOnTouchOutside(false);
    requireNonNull(dialog).getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(false);

    TextInputLayout editTextName = dialog.findViewById(R.id.dialog_text_input_layout);
    assert editTextName != null;

    setupTextInputValidation(context, rootView, dialog, editTextName, callback);

    requireNonNull(editTextName.getEditText()).requestFocus();
  }

  /**
   * Shows a confirmation dialog for user deletion
   */
  public static void showDeleteConfirmationDialog(@NonNull Context context,
                                                  @NonNull String userName,
                                                  @NonNull ConfirmationDialogCallback callback) {
    new MaterialAlertDialogBuilder(context)
      .setTitle("Delete User")
      .setMessage("Are you sure you want to delete \"" + userName + "\"?")
      .setPositiveButton("Delete", (dialog, which) -> callback.onConfirmed())
      .setNegativeButton("Cancel", (dialog, which) -> callback.onCancelled())
      .setCancelable(true)
      .show();
  }

  /**
   * Shows a generic confirmation dialog
   */
  public static void showConfirmationDialog(@NonNull Context context,
                                            @NonNull String title,
                                            @NonNull String message,
                                            @NonNull String positiveButtonText,
                                            @NonNull String negativeButtonText,
                                            @NonNull ConfirmationDialogCallback callback) {
    new MaterialAlertDialogBuilder(context)
      .setTitle(title)
      .setMessage(message)
      .setPositiveButton(positiveButtonText, (dialog, which) -> callback.onConfirmed())
      .setNegativeButton(negativeButtonText, (dialog, which) -> callback.onCancelled())
      .setCancelable(true)
      .show();
  }

  private static void setupTextInputValidation(@NonNull Context context,
                                               @Nullable View rootView,
                                               @NonNull AlertDialog dialog,
                                               @NonNull TextInputLayout editTextName,
                                               @NonNull EnrollmentDialogCallback callback) {

    requireNonNull(editTextName.getEditText()).addTextChangedListener(new TextWatcher() {
      @Override
      public void beforeTextChanged(CharSequence s, int start, int count, int after) { }

      @Override
      public void onTextChanged(CharSequence s, int start, int before, int count) { }

      @Override
      public void afterTextChanged(Editable s) {
        String userName = requireNonNull(editTextName.getEditText().getText()).toString();
        boolean emptyText = isNull(userName) || userName.trim().isEmpty();
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(!emptyText);
      }
    });

    requireNonNull(editTextName.getEditText()).setOnKeyListener((v1, keyCode, event) -> {
      String userName = requireNonNull(editTextName.getEditText().getText()).toString();
      boolean emptyText = isNull(userName) || userName.trim().isEmpty();
      dialog.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(!emptyText);

      if ((event.getAction() == KeyEvent.ACTION_DOWN) && (keyCode == KeyEvent.KEYCODE_ENTER)) {
        if (emptyText) {
          if (rootView != null) {
            SnackbarHelper.showSnackbarMessage(rootView, "Please enter a name!", context);
          }
        }
        else {
          hideKeyboard(context, editTextName);
          dialog.dismiss();
          callback.onEnrollmentConfirmed(userName);
        }
        return true;
      }
      return false;
    });
  }

  /**
   * Shows a custom file selector dialog for selecting images
   */
  public static void showImageFileSelector(@NonNull Context context,
                                           @NonNull FileSelectionCallback callback) {
    MaterialAlertDialogBuilder builder = new MaterialAlertDialogBuilder(context);
    builder.setTitle("Select Image");

    View dialogView = LayoutInflater.from(context).inflate(R.layout.dialog_file_selector, null);
    RecyclerView recyclerView = dialogView.findViewById(R.id.file_list_recycler_view);
    recyclerView.setLayoutManager(new LinearLayoutManager(context));

    // Query images from MediaStore
    List<ImageFileItem> imageFiles = queryImageFiles(context);

    builder.setView(dialogView);

    builder.setNegativeButton("Cancel", (dialog, which) -> {
      dialog.dismiss();
      callback.onCancelled();
    });

    builder.setCancelable(true);
    AlertDialog dialog = builder.show();

    FileAdapter adapter = new FileAdapter(imageFiles, (uri) -> {
      dialog.dismiss();
      callback.onFileSelected(uri);
    });

    recyclerView.setAdapter(adapter);
  }

  private static List<ImageFileItem> queryImageFiles(@NonNull Context context) {
    List<ImageFileItem> imageFiles = new ArrayList<>();

    Uri collection = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
    String[] projection = new String[]{
      MediaStore.Images.Media._ID,
      MediaStore.Images.Media.DISPLAY_NAME,
      MediaStore.Images.Media.DATA
    };

    String sortOrder = MediaStore.Images.Media.DATE_MODIFIED + " DESC";

    try (Cursor cursor = context.getContentResolver().query(
      collection,
      projection,
      null,
      null,
      sortOrder
    )) {
      if (cursor != null) {
        int idColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media._ID);
        int nameColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DISPLAY_NAME);
        int dataColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATA);

        while (cursor.moveToNext()) {
          long id = cursor.getLong(idColumn);
          String name = cursor.getString(nameColumn);
          String path = cursor.getString(dataColumn);

          Uri contentUri = Uri.withAppendedPath(
            MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
            String.valueOf(id)
          );

          imageFiles.add(new ImageFileItem(name, path, contentUri));
        }
      }
    }
    catch (Exception e) {
      Timber.e(e, "Error querying image files");
    }

    return imageFiles;
  }

  private static void hideKeyboard(@NonNull Context context, @NonNull View view) {
    try {
      var imm = (InputMethodManager)context.getSystemService(Context.INPUT_METHOD_SERVICE);
      if (view != null) {
        imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
      }
    }
    catch (Exception e) {
      Timber.w(e, "Error hiding keyboard");
    }
  }

  public interface EnrollmentDialogCallback {
    void onEnrollmentConfirmed(@NonNull String userName);

    void onEnrollmentCancelled();
  }

  public interface ConfirmationDialogCallback {
    void onConfirmed();

    void onCancelled();
  }

  public interface FileSelectionCallback {
    void onFileSelected(@NonNull Uri uri);

    void onCancelled();
  }

  // Image file item model
  private static class ImageFileItem {
    final String name;
    final String path;
    final Uri uri;

    ImageFileItem(String name, String path, Uri uri) {
      this.name = name;
      this.path = path;
      this.uri = uri;
    }
  }

  // RecyclerView adapter for file list
  private static class FileAdapter extends RecyclerView.Adapter<FileAdapter.FileViewHolder> {
    private final List<ImageFileItem> items;
    private final OnItemClickListener listener;

    interface OnItemClickListener {
      void onItemClick(Uri uri);
    }

    FileAdapter(List<ImageFileItem> items, OnItemClickListener listener) {
      this.items = items;
      this.listener = listener;
    }

    @NonNull
    @Override
    public FileViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
      View view = LayoutInflater.from(parent.getContext())
        .inflate(R.layout.item_file, parent, false);
      return new FileViewHolder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull FileViewHolder holder, int position) {
      ImageFileItem item = items.get(position);
      holder.fileName.setText(item.name);

      // Load thumbnail
      try {
        Bitmap thumbnail = BitmapFactory.decodeFile(item.path);
        if (thumbnail != null) {
          holder.fileIcon.setImageBitmap(thumbnail);
        }
        else {
          holder.fileIcon.setImageResource(android.R.drawable.ic_menu_gallery);
        }
      }
      catch (Exception e) {
        holder.fileIcon.setImageResource(android.R.drawable.ic_menu_gallery);
      }

      holder.itemView.setOnClickListener(v -> {
        listener.onItemClick(item.uri);
      });
    }

    @Override
    public int getItemCount() {
      return items.size();
    }

    static class FileViewHolder extends RecyclerView.ViewHolder {
      ImageView fileIcon;
      TextView fileName;

      FileViewHolder(@NonNull View itemView) {
        super(itemView);
        fileIcon = itemView.findViewById(R.id.file_icon);
        fileName = itemView.findViewById(R.id.file_name);
      }
    }
  }
}

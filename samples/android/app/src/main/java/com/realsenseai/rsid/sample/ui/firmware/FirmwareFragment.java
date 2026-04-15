// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.firmware;


import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.database.Cursor;
import android.hardware.usb.UsbDevice;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.PowerManager;
import android.provider.OpenableColumns;
import android.text.Html;
import android.text.InputType;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.ProgressBar;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModelProvider;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import com.google.android.material.textfield.TextInputLayout;
import com.realsenseai.rsid.api.DeviceController;
import com.realsenseai.rsid.api.DeviceType;
import com.realsenseai.rsid.api.SerialConfig;
import com.realsenseai.rsid.api.Status;
import com.realsenseai.rsid.sample.R;
import com.realsenseai.rsid.sample.databinding.FragmentFirmwareBinding;
import com.realsenseai.rsid.sample.ui.preview.PreviewViewModel;
import com.realsenseai.rsid.sample.ui.shared.RealSenseIdSharedViewModel;
import com.realsenseai.rsid.sample.util.FWUpdateHelper;
import com.realsenseai.rsid.sample.util.FileDownloader;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import com.realsenseai.rsid.sample.util.UsbDevicesReceiver;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import timber.log.Timber;

public class FirmwareFragment extends Fragment implements UsbDevicesReceiver.UsbListener {
  private final AtomicBoolean isFlashing = new AtomicBoolean(false);
  private final AtomicBoolean failed = new AtomicBoolean(false);
  private FragmentFirmwareBinding binding;
  private Thread flashFirmwareThread;
  private Thread firmwareThread;
  private PowerManager.WakeLock wakeLock;
  private AlertDialog flashDialog;
  private ExecutorService executorService;
  private volatile PreviewViewModel previewViewModel;
  private ActivityResultLauncher<Intent> firmwareFilePicker;
  private RealSenseIdSharedViewModel sharedViewModel;

  private AlertDialog blockingProgressDialog;


  @Override
  public void onCreate(@Nullable Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    executorService = Executors.newSingleThreadExecutor();

    sharedViewModel = new ViewModelProvider(requireActivity()).get(RealSenseIdSharedViewModel.class);

    SDKWrapper.INSTANCE.closeConnection();
  }

  private void updateButtonState() {
    if (binding == null) {
      Timber.e("FirmwareFragment: updateButtonState called but binding is null!");
      return;
    }

    boolean flashFirmwareEnabled = true;
    boolean flashFirmwareResourceEnabled = true;
    boolean flashFirmwareSelectFileEnabled = true;

    if (sharedViewModel.getDeviceType().getValue() == DeviceType.Unknown) {
      flashFirmwareEnabled = false;
      flashFirmwareResourceEnabled = false;
      flashFirmwareSelectFileEnabled = false;
    }
    else if (sharedViewModel.getDeviceType().getValue() == DeviceType.F50x) {
      flashFirmwareEnabled = false;
    }
    binding.buttonFlashFirmware.setEnabled(flashFirmwareEnabled);
    binding.buttonFlashFirmwareResource.setEnabled(flashFirmwareResourceEnabled);
    binding.buttonFlashFirmwareSelectFile.setEnabled(flashFirmwareSelectFileEnabled);
  }

  public View onCreateView(@NonNull LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
    FirmwareViewModel firmwareViewModel = new ViewModelProvider(this).get(FirmwareViewModel.class);

    binding = FragmentFirmwareBinding.inflate(inflater, container, false);
    View root = binding.getRoot();
    updateButtonState();
    initializeUI(firmwareViewModel);
    setupClickListeners();

    // Initialize the file picker launcher
    firmwareFilePicker = registerForActivityResult(new ActivityResultContracts.StartActivityForResult(), result -> {
      if (result.getResultCode() == Activity.RESULT_OK && result.getData() != null) {
        Uri selectedFileUri = result.getData().getData();
        if (selectedFileUri != null) {
          handleSelectedFirmwareFile(selectedFileUri);
        }
      }
    });

    observeSharedState();
    return root;
  }

  private void observeSharedState() {
    // Update button state based on device type
    sharedViewModel.getDeviceType().observe(getViewLifecycleOwner(), deviceType -> {
      updateButtonState();
    });

    // Update UI based on connection state
    sharedViewModel.getUsbConnectionState().observe(getViewLifecycleOwner(), isConnected -> {
      if (isConnected) {
        refreshFirmwareVersionsViews(true);
      }
    });
  }

  private void initializeUI(FirmwareViewModel firmwareViewModel) {
    binding.textHost.setText(com.realsenseai.rsid.api.RealSenseID.Version());

    firmwareViewModel.getText().observe(getViewLifecycleOwner(),
                                        html -> binding.textFirmware.setText(Html.fromHtml(html, Html.FROM_HTML_MODE_COMPACT)));
  }

  private void setupClickListeners() {
    binding.buttonRefresh.setOnClickListener(v -> {
      Timber.d("Reloading firmware versions");
      refreshFirmwareVersionsViews(true);
    });

    binding.buttonFlashFirmware.setOnClickListener(v -> showFlashConfirmationDialog(this::downloadFirmware));
    binding.buttonFlashFirmwareResource.setOnClickListener(v -> showFlashConfirmationDialog(this::extractResourcesFirmware));
    binding.buttonFlashFirmwareSelectFile.setOnClickListener(v -> pickFirmwareFile());
    binding.buttonFetchDeviceLogs.setOnClickListener(v -> fetchDeviceLogs());
  }

  private void showSpinner() {
    runOnUiThreadSafe(() -> {
      if (isAdded() && !isDetached() && binding != null && binding.circularProgress != null) {
        binding.circularProgress.setVisibility(View.VISIBLE);
      }
    });
  }

  private void hideSpinner() {
    runOnUiThreadSafe(() -> {
      if (isAdded() && !isDetached() && binding != null && binding.circularProgress != null) {
        binding.circularProgress.setVisibility(View.GONE);
      }
    });
  }

  private void fetchDeviceLogs() {
    firmwareThread = new Thread(() -> {
      synchronized (this) {
        DeviceController controller = SDKWrapper.INSTANCE.getDeviceController();
        if (controller == null) {
          Timber.w("Device not connected");
          showErrorDialog("Device Error", "Unable to get DeviceController handle. Please verify that device is connected.");
          return;
        }

        String[] logsStrArray = new String[1];
        showSpinner();
        controller.FetchLog(logsStrArray);
        controller.Disconnect();
        controller.delete();
        hideSpinner();
        Timber.d("Device logs: %s", logsStrArray[0]);

        runOnUiThreadSafe(() -> handleDeviceLogAction(logsStrArray[0])
        );
      }
    });

    firmwareThread.start();
  }


  private void handleDeviceLogAction(@Nullable String logContent) {
    if (logContent == null || logContent.trim().isEmpty()) {
      showErrorDialog("Log Error", "No log content available to save or share.");
      return;
    }

    // Show dialog with options to save or share
    new MaterialAlertDialogBuilder(requireContext())
      .setTitle("Device Log")
      .setMessage("What would you like to do with the device log?")
      .setPositiveButton("Save to File", (dialog, which) -> saveLogToFile(logContent))
      .setNegativeButton("Share via Email", (dialog, which) -> shareLogViaEmail(logContent))
      .setNeutralButton("Cancel", null)
      .show();
  }

  private void showSuccessDialog(@NonNull String title, @NonNull String message) {
    runOnUiThreadSafe(
      () -> new MaterialAlertDialogBuilder(requireContext()).setTitle(title).setMessage(message).setPositiveButton("OK", null).show());
  }

  private void saveLogToFile(@NonNull String logContent) {
    Context context = requireContext();

    final EditText inputEditText = new EditText(context);
    inputEditText.setInputType(InputType.TYPE_CLASS_TEXT);
    inputEditText.setText("device-log.txt");

    TextInputLayout textInputLayout = new TextInputLayout(context);
    textInputLayout.setPadding((int)(19 * getResources().getDisplayMetrics().density), 0,
                               (int)(19 * getResources().getDisplayMetrics().density), 0);
    textInputLayout.addView(inputEditText);

    new MaterialAlertDialogBuilder(context)
      .setTitle("Save As")
      .setMessage("Enter a name for the log file. It will be saved in your Downloads folder.")
      .setView(textInputLayout)
      .setPositiveButton("Save",
                         (dialog, which) -> {
                           String fileName = inputEditText.getText().toString().trim();
                           if (fileName.isEmpty()) {
                             fileName = "device-log-" + System.currentTimeMillis() + ".txt";
                             showErrorDialog("Invalid Filename", "Filename cannot be empty. Using default: " + fileName);
                           }

                           if (!fileName.endsWith(".txt")) {
                             fileName += ".txt";
                           }

                           checkAndSaveFile(logContent, fileName);
                         })
      .setNegativeButton("Cancel", null)
      .show();
  }

  private void checkAndSaveFile(String logContent, @NonNull String fileName) {
    File downloadsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
    if (downloadsDir == null) {
      showErrorDialog("Save Error", "Downloads directory not available.");
      return;
    }

    if (!downloadsDir.exists()) {
      if (!downloadsDir.mkdirs()) {
        showErrorDialog("Save Error", "Could not create Downloads directory.");
        return;
      }
    }

    File logFile = new File(downloadsDir, fileName);

    if (logFile.exists()) {
      new MaterialAlertDialogBuilder(requireContext())
        .setTitle("Confirm Save As")
        .setMessage(fileName + " already exists.\nDo you want to replace it?")
        .setPositiveButton("Replace", (dialog, which) -> {
          performSaveOperation(logContent, logFile, fileName);
        })
        .setNegativeButton("Cancel", (dialog, which) -> {
          saveLogToFile(logContent);
        })
        .show();
    }
    else {
      performSaveOperation(logContent, logFile, fileName);
    }
  }

  private void performSaveOperation(String logContent, @NonNull File logFile, @NonNull String originalFileNameForLogging) {
    try (FileOutputStream fos = new FileOutputStream(logFile)) { // logFile already includes directory
      fos.write(logContent.getBytes());
      fos.flush();
      showSuccessDialog("File Saved", "Device log saved to: " + logFile.getAbsolutePath());
    }
    catch (Exception e) {
      Timber.e(e, "Error saving log to file: %s", logFile.getAbsolutePath());
      showErrorDialog("Save Error", "Failed to save log file '" + originalFileNameForLogging + "': " + e.getMessage());
    }
  }

  private void shareLogViaEmail(String logContent) {
    try {
      File tempFile = new File(requireContext().getCacheDir(), "device-log.txt");

      try (FileOutputStream fos = new FileOutputStream(tempFile)) {
        fos.write(logContent.getBytes());
        fos.flush();
      }

      Uri fileUri = androidx.core.content.FileProvider.getUriForFile(requireContext(),
                                                                     requireContext().getPackageName() + ".fileprovider", tempFile);

      Intent emailIntent = new Intent(Intent.ACTION_SEND);
      emailIntent.setType("text/plain");
      emailIntent.putExtra(Intent.EXTRA_SUBJECT, "Device Log Report");
      emailIntent.putExtra(Intent.EXTRA_TEXT, "Please find the device log attached.");
      emailIntent.putExtra(Intent.EXTRA_STREAM, fileUri);
      emailIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);

      Intent chooser = Intent.createChooser(emailIntent, "Send Device Log via Email");
      startActivity(chooser);
    }
    catch (Exception e) {
      Timber.e(e, "Error sharing log via email");
      showErrorDialog("Share Error", "Failed to share log via email: " + e.getMessage());
    }
  }

  private void showFlashConfirmationDialog(@NonNull Runnable onConfirm) {
    if (isFlashing.get()) {
      Timber.w("Flash operation already in progress");
      return;
    }

    new MaterialAlertDialogBuilder(requireContext())
      .setTitle(Messages.FW_FLASH_TITLE)
      .setMessage(Messages.FW_FLASH_MESSAGE)
      .setPositiveButton("Yes",
                         (dialog, which) -> {
                           dialog.dismiss();
                           onConfirm.run();
                         })
      .setNegativeButton("Cancel", (dialog, which) -> dialog.dismiss())
      .setCancelable(false)
      .show();
  }

  @SuppressLint("SourceLockedOrientationActivity")
  private void preFlash() {
    if (!isFlashing.compareAndSet(false, true)) {
      Timber.w("Flash operation already in progress");
      return;
    }

    runOnUiThreadSafe(() -> {
      // Keep screen on
      requireActivity().getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

      // Acquire wake lock
      PowerManager pm = (PowerManager)requireContext().getSystemService(Context.POWER_SERVICE);
      if (pm != null) {
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, getClass().getName());
        wakeLock.acquire(Config.WAKE_LOCK_TIMEOUT_MS);
      }

      // Lock current orientation
      lockScreenOrientation();
    });
  }

  @SuppressLint("SourceLockedOrientationActivity")
  private void lockScreenOrientation() {
    int currentOrientation = getResources().getConfiguration().orientation;
    if (currentOrientation == Configuration.ORIENTATION_LANDSCAPE) {
      requireActivity().setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }
    else {
      requireActivity().setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT);
    }
  }

  private void postFlash() {
    isFlashing.set(false);

    runOnUiThreadSafe(() -> {
      requireActivity().getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
      releaseWakeLock();
      requireActivity().setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
    });
  }

  private void releaseWakeLock() {
    if (wakeLock != null && wakeLock.isHeld()) {
      try {
        wakeLock.release();
        wakeLock = null;
      }
      catch (Exception e) {
        Timber.w(e, "Error releasing wake lock");
      }
    }
  }

  private LinearProgressIndicator showFlashDialog(@NonNull String title, @NonNull String message) {
    LinearProgressIndicator progressIndicator = new LinearProgressIndicator(requireContext());
    progressIndicator.setPadding(40, 0, 40, 0);

    runOnUiThreadSafe(() -> {
      MaterialAlertDialogBuilder builder = new MaterialAlertDialogBuilder(requireContext())
        .setTitle(title)
        .setMessage(message)
        .setView(progressIndicator)
        .setCancelable(false);

      flashDialog = builder.show();
    });

    return progressIndicator;
  }

  private void dismissFlashDialog() {
    runOnUiThreadSafe(() -> {
      if (flashDialog != null && flashDialog.isShowing()) {
        flashDialog.dismiss();
        flashDialog = null;
      }
    });
  }

  private void downloadFirmware() {
    try {
      String fwUrl = getFirmwareUrl();
      startFirmwareDownload(fwUrl);
    }
    catch (Exception e) {
      Timber.e(e, "Error getting firmware URL");
      showErrorDialog(Messages.SKU_ERROR_TITLE, "Error: " + e.getMessage());
    }
  }

  private String getFirmwareUrl() {
    if (getDeviceType() == DeviceType.F50x) {
      throw new RuntimeException("ERROR: There is no URL for F50x FW file!!!");
      // return Config.FW_F50X_URL;
    }

    // F45x
    int sku = SDKWrapper.INSTANCE.getSKUVersion();
    return switch (sku) {
      case 1 -> Config.FW_SKU1_URL;
      case 2 -> Config.FW_SKU2_URL;
      default -> throw new IllegalStateException("Unsupported SKU version: " + sku);
    };
  }

  private void startFirmwareDownload(String fwUrl) {
    LinearProgressIndicator progressBar = showFlashDialog(Messages.DOWNLOADING_TITLE, Messages.DOWNLOADING_MESSAGE);

    MutableLiveData<Integer> downloadProgress = new MutableLiveData<>();
    downloadProgress.observe(getViewLifecycleOwner(), progressBar::setProgress);

    FileDownloader downloader = new FileDownloader(downloadProgress);
    downloader.downloadFile(fwUrl, Config.FIRMWARE_FILENAME, new FirmwareDownloadCallback(downloadProgress));
  }

  private void extractResourcesFirmware() {
    try {
      String firmwarePath = extractFirmwareFromResources();
      flashFirmware(firmwarePath);
    }
    catch (Exception e) {
      Timber.e(e, "Error extracting firmware from resources");
      showErrorDialog(Messages.SKU_ERROR_TITLE, "Error: " + e.getMessage());
    }
  }

  private void pickFirmwareFile() {
    Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
    intent.setType("*/*");
    intent.addCategory(Intent.CATEGORY_OPENABLE);

    // Create a chooser to show file picker
    Intent chooser = Intent.createChooser(intent, "Select Firmware File (.bin)");

    try {
      firmwareFilePicker.launch(chooser);
    }
    catch (Exception ex) {
      showErrorDialog("File Picker Error", "No file manager found. Please install a file manager app.");
    }
  }

  private void handleSelectedFirmwareFile(Uri selectedFileUri) {
    String fileName = getFileName(selectedFileUri);

    // Check if the file has .bin extension
    if (fileName != null && fileName.toLowerCase().endsWith(".bin")) {
      String filePath = getFilePathFromUri(selectedFileUri);
      if (filePath != null) {
        showFlashConfirmationDialog(() -> flashFirmware(filePath));
      }
      else {
        showErrorDialog("File Error", "Could not access the selected file.");
      }
    }
    else {
      showErrorDialog("Invalid File", "Please select a file with .bin extension.");
    }
  }

  private String getFileName(Uri uri) {
    String fileName = uri.getLastPathSegment();

    // Try to get the actual display name if it's a content URI
    if ("content".equals(uri.getScheme())) {
      try (Cursor cursor = requireContext().getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
        if (cursor != null && cursor.moveToFirst()) {
          String displayName = cursor.getString(0);
          if (displayName != null) {
            fileName = displayName;
          }
        }
      }
      catch (Exception e) {
        Timber.w(e, "Could not get display name, using last path segment");
      }
    }

    return fileName;
  }

  private String getFilePathFromUri(Uri uri) {
    try {
      File tempFile = new File(requireContext().getCacheDir(), "selected_firmware.bin");

      try (InputStream inputStream = requireContext().getContentResolver().openInputStream(uri);
           FileOutputStream outputStream = new FileOutputStream(tempFile)) {

        if (inputStream == null) return null;

        byte[] buffer = new byte[8192];
        int bytesRead;
        while ((bytesRead = inputStream.read(buffer)) != -1) {
          outputStream.write(buffer, 0, bytesRead);
        }

        return tempFile.getAbsolutePath();
      }
    }
    catch (Exception e) {
      Timber.e(e, "Error copying selected file");
      return null;
    }
  }

  private String extractFirmwareFromResources() throws IOException {
    int resFile = 0;
    if (getDeviceType() == DeviceType.F50x) {
      resFile = R.raw.f500_release;
    }
    else {// F45x
      int sku = SDKWrapper.INSTANCE.getSKUVersion();
      resFile = switch (sku) {
        case 1 -> R.raw.f450_sku1_signed;
        case 2 -> R.raw.f450_sku2_signed;
        default -> throw new IllegalStateException("Unsupported SKU version: " + sku);
      };
    }

    File folder = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
                           FileDownloader.REALSENSE_ID_FOLDER);

    if (!folder.exists() && !folder.mkdir()) {
      throw new IOException("Can't create download folder");
    }

    File firmwareFile = new File(folder, Config.FIRMWARE_FILENAME);

    try (InputStream is = requireContext().getResources().openRawResource(resFile);
         FileOutputStream os = new FileOutputStream(firmwareFile)) {

      byte[] buffer = new byte[Config.BUFFER_SIZE];
      int bytesRead;
      while ((bytesRead = is.read(buffer)) != -1) {
        os.write(buffer, 0, bytesRead);
      }
    }

    return firmwareFile.getAbsolutePath();
  }

  private void flashFirmware(@NonNull String filePath) {
    dismissFlashDialog();
    preFlash();

    // Run compatibility check on a background thread before flashing
    flashFirmwareThread = new Thread(() -> {
      synchronized (this) {
        MutableLiveData<Integer> flashProgress = new MutableLiveData<>();
        FWUpdateHelper fwUpdateHelper = new FWUpdateHelper(requireContext(), flashProgress,
                                                           new FirmwareFlashCallback(flashProgress));

        var compatInfo = fwUpdateHelper.checkCompatibility(getDeviceType(), filePath);
        if (compatInfo == null) {
          postFlash();
          runOnUiThreadSafe(() -> showErrorDialog("Connection Error", "Unable to acquire serial port handle."));
          return;
        }

        // OTP SKU mismatch (F45x) - block
        if (!compatInfo.IsOtpSkuCompatible()) {
          String msg;
          if (compatInfo.getDeviceOtpSku() < 0) {
            msg = "Failed to determine OTP encryption SKU.\nCheck device connection and try again.";
          }
          else {
            msg = "Firmware OTP SKU mismatch: device is SKU" + compatInfo.getDeviceOtpSku() +
                  ", firmware targets SKU" + compatInfo.getExpectedOtpSku() +
                  ".\nMake sure the firmware file is for SKU" + compatInfo.getDeviceOtpSku() + " devices and try again.";
          }
          compatInfo.delete();
          postFlash();
          runOnUiThreadSafe(() -> showErrorDialog("Incompatible Firmware OTP SKU", msg));
          return;
        }

        // Secure boot mismatch (F460/F500) - block
        if (!compatInfo.IsSecureBootCompatible()) {
          String msg;
          if (compatInfo.getDeviceSecureBoot() < 0) {
            msg = "Failed to determine secure boot variant.\nCheck device connection and try again.";
          }
          else {
            String expectedName = compatInfo.getDeviceSecureBoot() == 0 ? "is NOT SIGNED" :
                                  "is SIGNED";
            msg = "Firmware security mismatch: make sure the firmware file " + expectedName + " and try again.";
          }
          compatInfo.delete();
          postFlash();
          runOnUiThreadSafe(() -> showErrorDialog("Incompatible Firmware Security Variant", msg));
          return;
        }

        // Device type mismatch - block
        if (!compatInfo.IsDeviceTypeCompatible()) {
          String msg;
          int connType = compatInfo.getConnectedDeviceType();
          int expType = compatInfo.getExpectedDeviceType();
          if (connType < 0 || expType < 0 || connType > 2 || expType > 2) {
            msg = "Failed to determine device type compatibility.\nCheck device connection and try again.";
          }
          else {
            String connName = connType == 0 ? "F45x" : connType == 1 ? "F46x" : "F50x";
            String binName = expType == 0 ? "F45x" : expType == 1 ? "F46x" : "F50x";
            msg =
              "Firmware device type mismatch: connected device (" + connName + ") does not match the selected firmware (" + binName + ").";
          }
          compatInfo.delete();
          postFlash();
          runOnUiThreadSafe(() -> showErrorDialog("Incompatible Firmware Device Type", msg));
          return;
        }

        // DB version mismatch - warn but allow proceed
        if (!compatInfo.IsDbCompatible()) {
          String msg =
            "DB version mismatch (device: v" + compatInfo.getDeviceDbVer() + ", firmware: v" + compatInfo.getExpectedDbVer() + ").\n\n"
            + "The database may be erased after the update.\nExport it first if needed.\n\n"
            + "Proceed with the firmware update?";
          CompletableFuture<Boolean> userChoice = new CompletableFuture<>();
          runOnUiThreadSafe(()
                              -> new MaterialAlertDialogBuilder(requireContext())
            .setTitle("DB Version Mismatch")
            .setMessage(msg)
            .setPositiveButton("Yes", (dialog, which) -> userChoice.complete(true))
            .setNegativeButton("No", (dialog, which) -> userChoice.complete(false))
            .setCancelable(false)
            .show());

          try {
            if (!userChoice.get()) {
              compatInfo.delete();
              postFlash();
              return;
            }
          }
          catch (Exception e) {
            compatInfo.delete();
            postFlash();
            return;
          }
        }

        compatInfo.delete();

        // All checks passed - proceed with flash
        runOnUiThreadSafe(() -> {
          LinearProgressIndicator progressBar = showFlashDialog(Messages.FLASHING_TITLE, Messages.FLASHING_MESSAGE);
          flashProgress.observe(getViewLifecycleOwner(), progressBar::setProgress);
        });

        fwUpdateHelper.flashFirmware(getDeviceType(), filePath);
      }
    });

    flashFirmwareThread.start();
  }

  private DeviceType getDeviceType() {
    return sharedViewModel.getCurrentDeviceType();
  }

  private void showBlockingProgress(String title, String message) {
    runOnUiThreadSafe(() -> {
      if (blockingProgressDialog != null && blockingProgressDialog.isShowing()) {
        return; // Already showing
      }

      ProgressBar progressBar = new ProgressBar(requireContext());
      progressBar.setIndeterminate(true);

      blockingProgressDialog = new MaterialAlertDialogBuilder(requireContext())
        .setTitle(title)
        .setMessage(message)
        .setView(progressBar)
        .setCancelable(false) // This prevents user from dismissing
        .create();

      blockingProgressDialog.show();
    });
  }

  private void hideBlockingProgress() {
    runOnUiThreadSafe(() -> {
      if (blockingProgressDialog != null && blockingProgressDialog.isShowing()) {
        blockingProgressDialog.dismiss();
        blockingProgressDialog = null;
      }
    });
  }

  private void refreshFirmwareVersionsViews(Boolean forceReload) {
    FirmwareViewModel firmwareViewModel = new ViewModelProvider(this).get(FirmwareViewModel.class);

    if (forceReload || shouldRefreshFirmwareInfo(firmwareViewModel)) {
      binding.textFirmware.setText(Messages.LOADING_TEXT);

      executorService.submit(() -> {
        try {
          showBlockingProgress("Please wait", "Fetching camera firmware version information.");
          String firmwareInfo = getFirmwareVersionInfo();
          updateFirmwareInfoOnUI(firmwareViewModel, firmwareInfo);
        }
        catch (Exception e) {
          Timber.e(e, "Error refreshing firmware versions");
          updateFirmwareInfoOnUI(firmwareViewModel, Messages.ERROR_LOADING_TEXT);
        }
        finally {
          hideBlockingProgress();
        }
      });
    }
  }

  private boolean shouldRefreshFirmwareInfo(FirmwareViewModel viewModel) {
    String currentValue = viewModel.getText().getValue();
    return currentValue == null || currentValue.isEmpty();
  }

  private String getFirmwareVersionInfo() {
    synchronized (this) {
      var controller = SDKWrapper.INSTANCE.getDeviceController();
      if (controller == null) {
        return Messages.DEVICE_NOT_CONNECTED_TEXT;
      }

      String[] fwArr = new String[1];
      Status status = controller.QueryFirmwareVersion(fwArr);
      controller.Disconnect();
      controller.delete();

      if (status == Status.Ok && fwArr[0] != null) {
        return formatFirmwareInfo(fwArr[0]);
      }
      else {
        return Messages.QUERY_FAILED_TEXT;
      }
    }
  }

  private String formatFirmwareInfo(String firmwareString) {
    StringBuilder html = new StringBuilder("<ul>");
    String[] models = firmwareString.split("\\|");

    for (String model : models) {
      String[] parts = model.split(":", 2);
      if (parts.length == 2) {
        html.append("<li><b>").append(parts[0]).append("</b>: ").append(parts[1]).append("</li>");
      }
    }

    html.append("</ul>");
    return html.toString();
  }

  private void updateFirmwareInfoOnUI(FirmwareViewModel viewModel, String info) {
    runOnUiThreadSafe(() -> viewModel.setText(info));
  }

  private void runOnUiThreadSafe(Runnable action) {
    if (isAdded() && !isDetached() && getActivity() != null) {
      requireActivity().runOnUiThread(action);
    }
  }

  private void showErrorDialog(String title, String message) {
    runOnUiThreadSafe(
      () -> new MaterialAlertDialogBuilder(requireContext()).setTitle(title).setMessage(message).setPositiveButton("OK", null).show());
  }

  private void showInfoDialog(String title, String message) {
    if (!isAdded() || getContext() == null) {
      return;
    }
    new MaterialAlertDialogBuilder(requireContext()).setTitle(title).setMessage(message).setPositiveButton("OK", null).show();
  }

  private void cleanupResources() {
    // Cancel ongoing operations
    if (flashFirmwareThread != null && !flashFirmwareThread.isInterrupted()) {
      flashFirmwareThread.interrupt();
    }

    // Release wake lock safely
    releaseWakeLock();

    // Dismiss dialogs
    dismissFlashDialog();

    // Reset flashing state
    isFlashing.set(false);

    // Shutdown executor
    if (executorService != null && !executorService.isShutdown()) {
      executorService.shutdown();
    }
    SDKWrapper.INSTANCE.closeConnection();
  }

  @Override
  public void onDestroyView() {
    super.onDestroyView();
    cleanupResources();
    binding = null;
  }

  @Override
  public void onDeviceDetached(@Nullable UsbDevice usbDevice) {
    Timber.i("FirmwareFragment: onDeviceDetached");
  }

  @Override
  public void onDeviceAttached(@Nullable UsbDevice usbDevice) {
    Timber.i("FirmwareFragment: onDeviceAttached");
    if (binding == null || !isAdded() || getView() == null) {
      Timber.e("FirmwareFragment: onDeviceAttached called but binding is null!");
    }
    updateButtonState();
    refreshFirmwareVersionsViews(true);
  }

  // Configuration constants
  private static final class Config {
    static final String FW_SKU1_URL = "https://github.com/realsenseai/RealSenseID/releases/download/v1.3.1/F450_8.2.0.300_SKU1_SIGNED.bin";
    static final String FW_SKU2_URL = "https://github.com/realsenseai/RealSenseID/releases/download/v1.3.1/F450_8.2.0.300_SKU2_SIGNED.bin";
    static final int WAKE_LOCK_TIMEOUT_MS = 20 * 60 * 1000; // 20 minutes
    static final int DEVICE_REBOOT_MAX_RETRIES = 20;
    static final int DEVICE_REBOOT_RETRY_DELAY_MS = 500;
    static final String FIRMWARE_FILENAME = "firmware.bin";
    static final int BUFFER_SIZE = 4096;
    static final int REQUEST_CODE_PICK_FIRMWARE = 1001;
  }

  // TODO: Extract to resources
  private static final class Messages {
    static final String FW_FLASH_MESSAGE = """
      1. This operation is not reversible by this Android app.
      2. You will not be able to use the app until the process is completed.
      3. You should check that there is sufficient battery to complete the operation.
      4. You should not disconnect the camera during the flashing process.
      5. Do not use other apps while flashing and do not move this app to the background.
      
      Are you sure you want to proceed with flash firmware?""";
    static final String FW_FLASH_TITLE = "Flash Firmware: Before proceeding";
    static final String DOWNLOADING_TITLE = "Downloading Firmware";
    static final String DOWNLOADING_MESSAGE = "Downloading Firmware file from GitHub releases.";
    static final String FLASHING_TITLE = "Flashing Firmware";
    static final String FLASHING_MESSAGE = "Please standby and keep the application open in the foreground.";
    static final String SUCCESS_TITLE = "Firmware Flashed Successfully";
    static final String SUCCESS_MESSAGE = "Operation completed successfully! Waiting for device to reboot.";
    static final String SKU_ERROR_TITLE = "SKU Error";
    static final String DOWNLOAD_ERROR_TITLE = "Firmware Download Error";
    static final String FLASH_ERROR_TITLE = "Firmware Flash Error";
    static final String LOADING_TEXT = "Loading...\n";
    static final String ERROR_LOADING_TEXT = "<ul><li>Error loading firmware info</li></ul>";
    static final String DEVICE_NOT_CONNECTED_TEXT = "<ul><li>Device not connected</li></ul>";
    static final String QUERY_FAILED_TEXT = "<ul><li>Failed to query firmware version</li></ul>";
  }

  private class FirmwareDownloadCallback implements FileDownloader.FileDownloaderCallback {
    private final MutableLiveData<Integer> downloadProgress;

    public FirmwareDownloadCallback(MutableLiveData<Integer> downloadProgress) {
      this.downloadProgress = downloadProgress;
    }

    @Override
    public void onDownloadSuccess(String filePath) {
      Timber.i("Download successful: %s", filePath);
      runOnUiThreadSafe(() -> {
        dismissFlashDialog();
        downloadProgress.removeObservers(getViewLifecycleOwner());
        flashFirmware(filePath);
      });
    }

    @Override
    public void onDownloadFailure(String message) {
      Timber.e("Download failed: %s", message);
      runOnUiThreadSafe(() -> {
        downloadProgress.removeObservers(getViewLifecycleOwner());
        postFlash();
        dismissFlashDialog();
        showErrorDialog(Messages.DOWNLOAD_ERROR_TITLE, "Encountered error while downloading firmware file. Error: " + message);
      });
    }
  }

  private class FirmwareFlashCallback implements FWUpdateHelper.FWUpdateCallback {
    private final MutableLiveData<Integer> flashProgress;

    public FirmwareFlashCallback(MutableLiveData<Integer> flashProgress) {
      this.flashProgress = flashProgress;
    }

    @Override
    public void onUpdateSuccess() {
      Timber.i("Firmware flash successful");
      getPreviewViewModel().setIsCompatible(false); // Enforce compat recheck
      failed.set(false);

      runOnUiThreadSafe(() -> flashProgress.removeObservers(getViewLifecycleOwner()));
      handleFlashSuccess();
    }

    @Override
    public void onUpdateFailure(String message) {
      Timber.e("Firmware flash failed: %s", message);
      getPreviewViewModel().setIsCompatible(false); // Enforce compat recheck

      failed.set(true);
      runOnUiThreadSafe(() -> {
        synchronized (this) {
          SDKWrapper.INSTANCE.closeConnection();
          flashProgress.removeObservers(getViewLifecycleOwner());
          postFlash();
          dismissFlashDialog();
          showErrorDialog(Messages.FLASH_ERROR_TITLE,
                          "Encountered error while flashing firmware file. Error: " + message);
        }
      });
    }

    private void handleFlashSuccess() {
      try {
        // This is a work-around a bug in latest fw upgrade
        Thread.sleep(3000);
      }
      catch (InterruptedException e) {
        Thread.currentThread().interrupt();
      }

      // This is a work-around a bug in latest FW upgrade code.
      if (failed.get()) {
        return;
      }

      runOnUiThreadSafe(() -> showErrorDialog(Messages.SUCCESS_TITLE, Messages.SUCCESS_MESSAGE));

      // Wait for device reboot (blocking, runs on background thread)
      waitForDeviceReboot();
      postFlash();
      dismissFlashDialog();
    }

    private void waitForDeviceReboot() {
      SerialConfig config = null;
      int tries = Config.DEVICE_REBOOT_MAX_RETRIES;

      while (config == null && tries > 0) {
        try {
          tries--;
          Thread.sleep(Config.DEVICE_REBOOT_RETRY_DELAY_MS);
          config = SDKWrapper.INSTANCE.getCachedOrNewSerialConfig();
        }
        catch (Exception ignored) {
          // Expected during reboot
        }
      }
    }

    private PreviewViewModel getPreviewViewModel() {
      if (previewViewModel == null) {
        synchronized (this) {
          if (previewViewModel == null) {
            previewViewModel = new ViewModelProvider(requireActivity()).get(PreviewViewModel.class);
          }
        }
      }
      return previewViewModel;
    }
  }
}

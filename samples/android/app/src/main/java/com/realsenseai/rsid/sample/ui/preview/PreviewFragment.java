// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.preview;

import static androidx.core.util.ObjectsCompat.requireNonNull;
import static java.util.Objects.nonNull;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Point;
import android.graphics.Rect;
import android.view.LayoutInflater;
import android.view.TextureView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.animation.DecelerateInterpolator;
import android.widget.ProgressBar;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModelProvider;
import com.google.android.material.button.MaterialButton;
import com.realsenseai.rsid.api.DeviceConfig;
import com.realsenseai.rsid.api.DeviceType;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.sample.MainActivity;
import com.realsenseai.rsid.sample.R;
import com.realsenseai.rsid.sample.callbacks.AuthenticationCallback;
import com.realsenseai.rsid.sample.callbacks.EnrollmentCallback;
import com.realsenseai.rsid.sample.databinding.ActivityMainBinding;
import com.realsenseai.rsid.sample.databinding.FragmentPreviewBinding;
import com.realsenseai.rsid.sample.ui.firmware.FirmwareViewModel;
import com.realsenseai.rsid.sample.ui.shared.RealSenseIdSharedViewModel;
import com.realsenseai.rsid.sample.ui.users.UserListViewModel;
import com.realsenseai.rsid.sample.util.CompatibilityChecker;
import com.realsenseai.rsid.sample.util.FileDownloader;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import com.realsenseai.rsid.sample.util.auth.DeviceAuthenticationHelper;
import com.realsenseai.rsid.sample.util.auth.HostAuthenticationHelper;
import com.realsenseai.rsid.sample.util.enroll.DeviceEnrollmentHelper;
import com.realsenseai.rsid.sample.util.enroll.HostEnrollmentHelper;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import org.opencv.android.Utils;
import org.opencv.core.Mat;
import org.opencv.imgproc.Imgproc;
import timber.log.Timber;

public class PreviewFragment extends BaseRealSenseIdPreviewFragment
  implements AuthenticationCallback, EnrollmentCallback {

  private static final ScheduledExecutorService SHARED_SCHEDULED_EXECUTOR =
    Executors.newScheduledThreadPool(
      4, r -> new Thread(r, "PreviewFragment-Scheduler"));

  private final ExecutorService taskExecutor =
    Executors.newCachedThreadPool(r -> {
      Thread t = new Thread(r, "PreviewFragment-Task");
      t.setDaemon(true);
      return t;
    });

  // Instance variables
  private final List<Rect> faceRects = new ArrayList<>();
  // Shared thread pools for better resource management
  // UI Feedback Helper
  private UIFeedbackHelper uiFeedbackHelper;

  // Lazy-initialized helpers (volatile for thread safety)
  private volatile UserListViewModel userListViewModel;
  private volatile FirmwareViewModel firmwareViewModel;
  private volatile HostEnrollmentHelper hostEnrollmentHelper;
  private volatile DeviceEnrollmentHelper deviceEnrollmentHelper;
  private volatile HostAuthenticationHelper hostAuthHelper;
  private volatile DeviceAuthenticationHelper deviceAuthHelper;
  private volatile CompatibilityChecker compatibilityChecker;
  private volatile FaceAuthenticator authenticator;

  // Held so register/unregister see the same lambda instance — `this::onDeviceAttachmentChanged`
  // evaluated at two source sites produces two distinct functional-interface objects, so naive
  // unregister with the method reference silently no-ops and the callback list grows on each
  // view recreation.
  private RealSenseIdSharedViewModel.DeviceAttachmentCallback deviceAttachmentCallback;

  // UI and lifecycle
  private FragmentPreviewBinding binding;

  private boolean useHostDb = false;

  @Nullable
  @Override
  protected View getRootView(@NonNull LayoutInflater layoutInflater,
                             @Nullable ViewGroup container) {
    // Clear collections
    synchronized (faceRects) {
      faceRects.clear();
    }

    var sharedViewModel = new ViewModelProvider(requireActivity())
      .get(RealSenseIdSharedViewModel.class);
    userListViewModel = new ViewModelProvider(requireActivity()).get(UserListViewModel.class);
    firmwareViewModel = new ViewModelProvider(requireActivity()).get(FirmwareViewModel.class);
    useHostDb = sharedViewModel.getUseHostDb().getValue() != null &&
                sharedViewModel.getUseHostDb().getValue() == true;

    uiFeedbackHelper = new UIFeedbackHelper(requireActivity()::runOnUiThread, SHARED_SCHEDULED_EXECUTOR);

    requireActivity().getWindow().addFlags(
      WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

    binding = FragmentPreviewBinding.inflate(layoutInflater, container, false);

    // Set binding reference in the UI feedback helper and initialize
    withFeedback(helper -> {
      uiFeedbackHelper.setBinding(binding);
      uiFeedbackHelper.initializeHint();
    });

    setupClickListeners();

    // Register callback for device attachment changes
    if (deviceAttachmentCallback == null) {
      deviceAttachmentCallback = this::onDeviceAttachmentChanged;
    }
    sharedViewModel.registerDeviceAttachmentCallback(deviceAttachmentCallback);

    // Observe one-to-one mode changes
    sharedViewModel.getOneToOneMode().observe(getViewLifecycleOwner(), this::updateButtonVisibilityForOneToOneMode);

    return binding.getRoot();
  }

  private void updateButtonVisibilityForOneToOneMode(boolean oneToOneModeEnabled) {
    if (binding == null || !isFragmentAttached()) {
      return;
    }

    if (oneToOneModeEnabled) {
      // Show one-to-one buttons, hide regular buttons
      binding.buttonEnrollCroppedImage.setVisibility(View.VISIBLE);
      binding.buttonAuthenticateCroppedImage.setVisibility(View.VISIBLE);

      binding.buttonEnroll.setVisibility(View.GONE);
      binding.buttonEnrollImage.setVisibility(View.GONE);
      binding.buttonAuthenticate.setVisibility(View.GONE);
    }
    else {
      // Hide one-to-one buttons, show regular buttons based on device mode
      binding.buttonEnrollCroppedImage.setVisibility(View.GONE);
      binding.buttonAuthenticateCroppedImage.setVisibility(View.GONE);

      // Restore regular button visibility based on device settings
      var sharedViewModel = new ViewModelProvider(requireActivity())
        .get(RealSenseIdSharedViewModel.class);
      var mode = sharedViewModel.getOperationMode().getValue();

      binding.buttonEnroll.setVisibility(getEnrollButtonVisibility(mode));
      binding.buttonEnrollImage.setVisibility(useHostDb ? View.VISIBLE : View.GONE);
      binding.buttonAuthenticate.setVisibility(View.VISIBLE);
    }
  }

  private int getEnrollButtonVisibility(DeviceConfig.AlgoFlow mode) {
    if (mode == null) // ToDo
    {
      Timber.d("Operation mode is null, showing enroll button");
      return View.VISIBLE;
    }
    if (mode == DeviceConfig.AlgoFlow.All ||
        mode == DeviceConfig.AlgoFlow.RecognitionOnly) {
      Timber.d("Operation mode is %s, showing enroll button", mode);
      return View.VISIBLE;
    }
    Timber.d("Operation mode is %s, not showing enroll button", mode);
    return View.GONE;
  }

  @Override
  public void onResume() {
    super.onResume();
    setRotateButtonVisibility(View.VISIBLE);
  }

  @Override
  public void onPause() {
    super.onPause();
    setRotateButtonVisibility(View.GONE);
  }

  private void setRotateButtonVisibility(int visibility) {
    var activity = requireActivity();
    if (activity instanceof MainActivity mainActivity) {
      var activityBinding = ActivityMainBinding.bind(mainActivity.findViewById(R.id.activity_container));
      if (activityBinding != null && activityBinding.appBarMain.buttonRotate != null) {
        activityBinding.appBarMain.buttonRotate.setVisibility(visibility);
      }
    }
  }

  private void executeInBackground(Runnable task) {
    executeInBackground(task, 15);
  }

  private void executeInBackground(Runnable task, int timeoutSeconds) {
    if (!isFragmentAttached()) {
      return;
    }

    Future<?> future = taskExecutor.submit(() -> {
      try {
        task.run();
      }
      catch (Exception e) {
        Timber.e(e, "Error executing background task");
      }
    });

    SHARED_SCHEDULED_EXECUTOR.schedule(() -> {
      if (!future.isDone()) {
        Timber.w("Background task timed out after %d seconds, cancelling...",
                 timeoutSeconds);
        boolean cancelled = future.cancel(false);
        Timber.d("Task cancellation result: %s", cancelled);
      }
    }, timeoutSeconds, TimeUnit.SECONDS);
  }

  private void handleImageSelection(android.net.Uri uri, @NonNull String userName) {
    if (!isFragmentAttached()) {
      return;
    }

    if (uri != null) {
      Timber.d("Selected URI: %s", uri);
      executeInBackground(() -> {
        if (!isFragmentAttached()) {
          return;
        }

        try {
          var bitmap = BitmapFactory.decodeStream(requireContext().getContentResolver().openInputStream(uri));

          // Force bitmap to ARGB_8888 format (OpenCV converts this to RGBA)
          if (bitmap.getConfig() != Bitmap.Config.ARGB_8888) {
            bitmap = bitmap.copy(Bitmap.Config.ARGB_8888, true);
          }

          Mat matImage = new Mat();
          Utils.bitmapToMat(requireNonNull(bitmap).copy(Bitmap.Config.ARGB_8888, false), matImage);
          Imgproc.cvtColor(matImage, matImage, Imgproc.COLOR_RGBA2BGR);

          var buffer = mat2Bytes(matImage);
          int width = matImage.width();
          int height = matImage.height();

          // Display the original bitmap (matImage is BGR for the C++ pipeline; matToBitmap would swap R↔B)
          Bitmap enrolledBitmap = bitmap;

          requireActivity().runOnUiThread(() -> {
            if (binding != null && isFragmentAttached()) {
              binding.enrolledImageOverlay.setImageBitmap(enrolledBitmap);
              binding.enrolledImageOverlay.setVisibility(View.VISIBLE);

              // Display debug info
              String debugInfo = String.format("HxW: %dx%d\nBytes: %d", height, width, buffer.length);
              binding.imageDebugInfo.setText(debugInfo);
              binding.imageDebugInfo.setVisibility(View.VISIBLE);

              // Auto-hide after 5 seconds
              new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() -> {
                if (binding != null && binding.enrolledImageOverlay != null) {
                  binding.enrolledImageOverlay.setVisibility(View.GONE);
                  binding.enrolledImageOverlay.setImageBitmap(null);
                  binding.imageDebugInfo.setVisibility(View.GONE);
                }
              }, 5000);
            }
          });

          getDeviceEnrollmentHelper().EnrollImageOneToOne(userName, buffer, width, height, PreviewFragment.this);
        }
        catch (Exception e) {
          Timber.e(e);
          enableButtons();
        }
      });
    }
    else {
      Timber.d("No media selected");
    }
  }

  private void rotateVideoGroup() {
    // Get toolbar rotate button
    var activity = requireActivity();
    MaterialButton toolbarRotateButton = null;
    if (activity instanceof com.realsenseai.rsid.sample.MainActivity mainActivity) {
      var activityBinding =
        com.realsenseai.rsid.sample.databinding.ActivityMainBinding.bind(mainActivity.findViewById(R.id.activity_container));
      if (activityBinding != null) {
        toolbarRotateButton = activityBinding.appBarMain.buttonRotate;
      }
    }

    // Block if button is already disabled (indicating animation in progress)
    if (toolbarRotateButton != null && !toolbarRotateButton.isEnabled()) {
      return;
    }

    // Disable button at start
    if (toolbarRotateButton != null) {
      toolbarRotateButton.setEnabled(false);
    }

    // Get current rotation directly from the view
    float currentRotation = binding.videoGroup.getRotation();
    if (currentRotation == 360f) {
      currentRotation = 0f;
      binding.videoGroup.setRotation(0f);
    }

    // Calculate next rotation: 0 -> 90 -> 180 -> 270 -> 360 (then reset to 0)
    float nextRotation = switch ((int)currentRotation) {
      case 0 -> 90f;
      case 90 -> 180f;
      case 180 -> 270f;
      case 270 -> 360f;
      default -> 0f;
    };

    // Get the parent container dimensions
    ViewGroup parent = (ViewGroup)binding.videoGroup.getParent();
    int parentWidth = parent.getWidth();
    int parentHeight = parent.getHeight();

    // Calculate scale factor to maximize area usage
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (nextRotation == 90f || nextRotation == 270f) {
      // For 90/270 degree rotations, we need to scale to fit the rotated dimensions
      // When rotated 90 degrees, width becomes height and vice versa

      if (parentWidth > parentHeight) {
        // Landscape parent - scale up to use full height when rotated
        scaleX = scaleY = (float)parentWidth / (float)parentHeight;
      }
      else {
        // Portrait parent - scale up to use full width when rotated
        scaleX = scaleY = (float)parentHeight / (float)parentWidth;
      }

      // Ensure we don't scale beyond reasonable bounds
      scaleX = Math.min(scaleX, 2.0f);
      scaleY = Math.min(scaleY, 2.0f);
    }
    else {
      // For 0/180 degree rotations, use normal scale
      scaleX = scaleY = 1.0f;
    }

    // Get toolbar rotate button for animation callback
    var activity2 = requireActivity();
    com.google.android.material.button.MaterialButton finalToolbarRotateButton = null;
    if (activity2 instanceof MainActivity mainActivity) {
      var activityBinding =
        ActivityMainBinding.bind(mainActivity.findViewById(R.id.activity_container));
      if (activityBinding != null) {
        finalToolbarRotateButton = activityBinding.appBarMain.buttonRotate;
      }
    }

    MaterialButton buttonToReenable = finalToolbarRotateButton;

    // Animate the rotation and scaling together
    binding.videoGroup.animate()
      .rotation(nextRotation)
      .scaleX(scaleX)
      .scaleY(scaleY)
      .setDuration(300)
      .setInterpolator(new DecelerateInterpolator())
      .setListener(new AnimatorListenerAdapter() {
        @Override
        public void onAnimationEnd(Animator animation) {
          super.onAnimationEnd(animation);
          if (buttonToReenable != null) {
            buttonToReenable.setEnabled(true);
          }
        }
      })
      .start();
  }

  private void setupClickListeners() {
    // Setup rotate button in toolbar
    var activity = requireActivity();
    if (activity instanceof MainActivity mainActivity) {
      var activityBinding = ActivityMainBinding.bind(mainActivity.findViewById(R.id.activity_container));
      if (activityBinding != null && activityBinding.appBarMain.buttonRotate != null) {
        activityBinding.appBarMain.buttonRotate.setOnClickListener(v -> rotateVideoGroup());
      }
    }

    binding.buttonAuthenticate.setOnClickListener(v -> {
      Timber.i("Authenticating (HostDb: %s)", useHostDb);

      // Clear face rectangles at start of new authentication session
      if (uiFeedbackHelper != null) {
        uiFeedbackHelper.clearFaceRectangles(UIFeedbackHelper.SessionType.AUTHENTICATION);
      }

      // Start new authentication session for frame dumping
      if (getPreviewHelper() != null) {
        getPreviewHelper().startNewAuthenticationSession();
      }

      executeInBackground(() -> {
        if (isFragmentAttached()) {
          disableButtons();
          if (useHostDb) {
            getHostAuthHelper().Authenticate(this);
          }
          else {
            getDeviceAuthHelper().Authenticate(this);
          }
        }
        else {
          Timber.w("Fragment is not attached, skipping image authentication");
        }
      });
    });


    binding.buttonEnroll.setOnClickListener(v -> {
      // Clear face rectangles at start of new enrollment session
      if (uiFeedbackHelper != null) {
        uiFeedbackHelper.clearFaceRectangles(UIFeedbackHelper.SessionType.ENROLLMENT);
      }
      Timber.i("Enrolling");
      enrollWithCamera();
    });

    binding.buttonEnrollImage.setVisibility(
      useHostDb ? View.VISIBLE : View.GONE); // FIXME: Disabled for now

    binding.buttonEnrollImage.setOnClickListener(v -> {
      // Clear face rectangles at start of new enrollment session
      if (uiFeedbackHelper != null) {
        uiFeedbackHelper.clearFaceRectangles(UIFeedbackHelper.SessionType.ENROLLMENT);
      }
      Timber.i("Enrolling Image");
      executeInBackground(() -> {
        if (isFragmentAttached()) {
          enrollImageFromUrl();
        }
        else {
          Timber.w("Fragment is not attached, skipping image enrollment");
        }
      }, 60); // Image enrollment can take long time!
    });

    // Note: Visibility for cropped image buttons is managed by one-to-one mode observer

    binding.buttonEnrollCroppedImage.setOnClickListener(v -> {
      // Clear face rectangles at start of new enrollment session
      if (uiFeedbackHelper != null) {
        uiFeedbackHelper.clearFaceRectangles(UIFeedbackHelper.SessionType.ENROLLMENT);
      }
      Timber.i("Crop/Enroll Image");
      enrollWithImage();
    });

    binding.buttonAuthenticateCroppedImage.setOnClickListener(v -> {
      // Clear face rectangles at start of new authentication session
      if (uiFeedbackHelper != null) {
        uiFeedbackHelper.clearFaceRectangles(UIFeedbackHelper.SessionType.AUTHENTICATION);
      }

      executeInBackground(() -> {
        if (isFragmentAttached()) {
          getDeviceAuthHelper().AuthenticateOne2One(this);
        }
        else {
          Timber.w("Fragment is not attached, skipping image authentication");
        }
      });
    });
  }

  private void enrollWithCamera() {
    if (!isFragmentAttached()) {
      return;
    }

    UIDialogHelper.showEnrollmentDialog(
      requireContext(), binding.getRoot(),
      new UIDialogHelper.EnrollmentDialogCallback() {
        @Override
        public void onEnrollmentConfirmed(@NonNull String userName) {
          executeInBackground(() -> {
            disableButtons();
            if (isFragmentAttached()) {
              getPreviewHelper().startNewEnrollmentSession(userName);
              if (useHostDb) {
                getHostEnrollmentHelper().Enroll(userName, PreviewFragment.this);
              }
              else {
                getDeviceEnrollmentHelper().Enroll(userName, PreviewFragment.this);
              }
            }
            else {
              Timber.w("Fragment is not attached, skipping enrollment");
            }
          });
        }

        @Override
        public void onEnrollmentCancelled() {
          enableButtons();
          // Handle cancellation if needed
        }
      });
  }

  private void enrollWithImage() {
    if (!isFragmentAttached()) {
      return;
    }

    UIDialogHelper.showEnrollmentDialog(
      requireContext(), binding.getRoot(),
      new UIDialogHelper.EnrollmentDialogCallback() {
        @Override
        public void onEnrollmentConfirmed(@NonNull String userName) {
          UIDialogHelper.showImageFileSelector(requireContext(), new UIDialogHelper.FileSelectionCallback() {
            @Override
            public void onFileSelected(@NonNull android.net.Uri uri) {
              handleImageSelection(uri, userName);
            }

            @Override
            public void onCancelled() {
              Timber.d("Image selection cancelled");
            }
          });
        }

        @Override
        public void onEnrollmentCancelled() {
          enableButtons();
          // Handle cancellation if needed
        }
      });
  }

  private void enrollImageFromUrl() {
    if (!isFragmentAttached()) {
      return;
    }

    var downloader = new FileDownloader(new MutableLiveData<>());
    downloader.downloadFile(
      Constants.DEFAULT_PNG_URL, Constants.DEFAULT_FILENAME,
      new FileDownloader.FileDownloaderCallback() {
        @Override
        public void onDownloadSuccess(String filePath) {
          if (!isFragmentAttached()) {
            return;
          }

          try {
            Mat image = new Mat();
            Bitmap bitmap = BitmapFactory.decodeFile(filePath);
            Utils.bitmapToMat(bitmap.copy(Bitmap.Config.ARGB_8888, true), image);
            Imgproc.cvtColor(image, image, Imgproc.COLOR_RGBA2BGR);
            var buffer = mat2Bytes(image);

            // Clear face rectangles at start of new enrollment session
            if (uiFeedbackHelper != null) {
              uiFeedbackHelper.clearFaceRectangles(UIFeedbackHelper.SessionType.ENROLLMENT);
            }

            getHostEnrollmentHelper().EnrollImage("Test Name", buffer, image.width(), image.height(), PreviewFragment.this);
            if (uiFeedbackHelper != null) {
              uiFeedbackHelper.showHint("Downloading / Enrolling");
            }
            disableButtons();
          }
          catch (Exception ex) {
            Timber.e(ex);
            enableButtons();
          }
        }

        @Override
        public void onDownloadFailure(String message) {
          Timber.e(message);
          enableButtons();
        }
      });
  }

  private void checkCompatibility() {
    if (!isFragmentAttached()) {
      return;
    }

    var viewModel = new ViewModelProvider(requireActivity())
      .get(RealSenseIdSharedViewModel.class);

    if (viewModel.isCurrentDeviceCompatible()) {
      Timber.d(
        "Device already verified as compatible, skipping compatibility check");
      enableButtons();
      return;
    }

    if (uiFeedbackHelper != null) {
      uiFeedbackHelper.showHint("Checking for compatibility!");
    }
    disableButtons();
    viewModel.setCompatibilityCheckRunning(true);

    executeInBackground(() -> {
      if (!isFragmentAttached()) {
        viewModel.setCompatibilityCheckRunning(false);
        return;
      }

      // Write-through: capture the raw firmware string into the firmware-info cache so the
      // firmware screen displays instantly on first landing without a redundant device query.
      compatibilityChecker = new CompatibilityChecker(requireContext(),
                                                      raw -> {
                                                        if (firmwareViewModel != null) firmwareViewModel.setRawFirmwareVersion(raw);
                                                      });
      if (authenticator != null) {
        authenticator.Disconnect();
      }
      compatibilityChecker.checkCompatibility(
        new CompatibilityChecker.CompatibilityCallback() {
          @Override
          public void onCompatible() {
            viewModel.setDeviceCompatible(true);
            viewModel.setCompatibilityCheckRunning(false);

            if (!isFragmentAttached()) {
              return;
            }

            if (uiFeedbackHelper != null) {
              uiFeedbackHelper.onCompatibilitySuccess("Device is compatible with host!");
            }
            requireActivity().runOnUiThread(() -> {
              if (isFragmentAttached()) {
                enableButtons();
              }
            });
          }

          @Override
          public void onIncompatible() {
            viewModel.setDeviceCompatible(false);
            viewModel.setCompatibilityCheckRunning(false);

            if (!isFragmentAttached()) {
              return;
            }

            if (uiFeedbackHelper != null) {
              uiFeedbackHelper.onCompatibilityFailure("Warning: Device is incompatible with host!");
            }
            requireActivity().runOnUiThread(() -> {
              if (isFragmentAttached()) {
                disableButtons();
              }
            });
          }

          @Override
          public void onError(@Nullable String errorMessage) {
            viewModel.setCompatibilityCheckRunning(false);

            if (!isFragmentAttached()) {
              return;
            }

            if (uiFeedbackHelper != null) {
              uiFeedbackHelper.onCompatibilityError("ERROR: Unable to check for compatibility!");
            }
            requireActivity().runOnUiThread(() -> {
              if (isFragmentAttached()) {
                disableButtons();
              }
            });
          }
        });
    });
  }

  DeviceEnrollmentHelper getDeviceEnrollmentHelper() {
    if (deviceEnrollmentHelper == null && isFragmentAttached()) {
      synchronized (this) {
        if (authenticator == null) {
          authenticator = SDKWrapper.INSTANCE.getAuthenticator();
        }
        if (deviceEnrollmentHelper == null && isFragmentAttached()) {
          deviceEnrollmentHelper = new DeviceEnrollmentHelper(requireContext(), authenticator);
        }
      }
    }
    return deviceEnrollmentHelper;
  }

  HostEnrollmentHelper getHostEnrollmentHelper() {
    if (hostEnrollmentHelper == null && isFragmentAttached()) {
      synchronized (this) {
        if (authenticator == null) {
          authenticator = SDKWrapper.INSTANCE.getAuthenticator();
        }
        if (hostEnrollmentHelper == null && isFragmentAttached()) {
          hostEnrollmentHelper = new HostEnrollmentHelper(requireContext(), authenticator);
        }
      }
    }
    return hostEnrollmentHelper;
  }

  HostAuthenticationHelper getHostAuthHelper() {
    if (isFragmentAttached()) { // hostAuthHelper == null
      synchronized (this) {
        if (authenticator == null) {
          authenticator = SDKWrapper.INSTANCE.getAuthenticator();
        }
        if (hostAuthHelper == null && isFragmentAttached()) {
          hostAuthHelper = new HostAuthenticationHelper(requireContext(), authenticator);
        }
      }
    }
    return hostAuthHelper;
  }

  DeviceAuthenticationHelper getDeviceAuthHelper() {
    if (isFragmentAttached()) { // deviceAuthHelper == null
      synchronized (this) {
        if (authenticator == null) {
          authenticator = SDKWrapper.INSTANCE.getAuthenticator();
        }
        if (deviceAuthHelper == null && isFragmentAttached()) {
          deviceAuthHelper = new DeviceAuthenticationHelper(requireContext(), authenticator);
        }
      }
    }
    return deviceAuthHelper;
  }

  public byte[] mat2Bytes(Mat mat) {
    byte[] buffer = new byte[(int)(mat.total() * mat.channels())];
    mat.get(0, 0, buffer);
    return buffer;
  }

  @Nullable
  @Override
  protected TextureView getVideoView() {
    if (isFragmentAttached() && binding != null) {
      return binding.videoTextureView;
    }
    Timber.w("Rendering TextureView is not available");
    return null;
  }

  @Nullable
  protected ProgressBar getLoadingIndicator() {
    if (isFragmentAttached() && binding != null) {
      return binding.progressSpinner;
    }
    Timber.w("Rendering ProgressBar is not available");
    return null;
  }

  @Override
  protected void refreshCompatibilityCheck() {
    checkCompatibility();
  }

  private void runOnUiThreadSafe(Runnable action) {
    if (isAdded() && !isDetached() && getActivity() != null) {
      requireActivity().runOnUiThread(action);
    }
  }

  private void withFeedback(java.util.function.Consumer<UIFeedbackHelper> action) {
    if (uiFeedbackHelper != null) {
      action.accept(uiFeedbackHelper);
    }
  }

  private void disableButtons() {
    runOnUiThreadSafe(() -> setButtonsEnabledState(false));
  }

  private void enableButtons() {
    runOnUiThreadSafe(() -> setButtonsEnabledState(true));
  }

  // Then modify the setButtonsEnabledState method:
  private void setButtonsEnabledState(boolean state) {
    if (nonNull(binding) && isFragmentAttached()) {

      // Animate text color change
      int fromColor = state ? getResources().getColor(
        android.R.color.primary_text_light, null)
                            : getResources().getColor(
                              android.R.color.primary_text_dark, null);

      int toColor =
        state
        ? getResources().getColor(android.R.color.primary_text_dark, null)
        : getResources().getColor(android.R.color.primary_text_light,
                                  null);

      android.animation.ValueAnimator colorAnimator =
        android.animation.ValueAnimator.ofArgb(fromColor, toColor);
      colorAnimator.setDuration(100); // ms
      colorAnimator.addUpdateListener(animation -> {
        if (nonNull(binding) && isFragmentAttached()) {
          int animatedColor = (int)animation.getAnimatedValue();
          binding.buttonAuthenticate.setTextColor(animatedColor);
          binding.buttonEnroll.setTextColor(animatedColor);
          binding.buttonEnrollImage.setTextColor(animatedColor);
          binding.buttonEnrollCroppedImage.setTextColor(animatedColor);
          binding.buttonAuthenticateCroppedImage.setTextColor(animatedColor);
        }
      });

      colorAnimator.addListener(new android.animation.AnimatorListenerAdapter() {
        @Override
        public void onAnimationEnd(android.animation.Animator animation) {
          // Set enabled state
          binding.buttonAuthenticate.setEnabled(state);
          binding.buttonEnroll.setEnabled(state);
          binding.buttonEnrollImage.setEnabled(state);
          binding.buttonEnrollCroppedImage.setEnabled(state);
          binding.buttonAuthenticateCroppedImage.setEnabled(state);
        }
      });

      colorAnimator.start();
    }
  }

  // Delegate UI feedback methods to UIFeedbackHelper
  @Override
  public void onAuthSuccess(@NonNull String user, @NonNull List<Rect> faceRects,
                            @NonNull List<List<Point>> faceLandmarks,
                            short score) {
    if (isFragmentAttached() && uiFeedbackHelper != null) {
      var sharedViewModel = new ViewModelProvider(requireActivity()).get(RealSenseIdSharedViewModel.class);
      uiFeedbackHelper.onAuthSuccess(user,
                                     faceRects,
                                     faceLandmarks,
                                     sharedViewModel.getCurrentCameraRotation(),
                                     getScaleFactor(),
                                     score);
    }

    // Hide enrolled image overlay after successful authentication
    if (isFragmentAttached() && binding != null) {
      requireActivity().runOnUiThread(() -> {
        if (binding != null && binding.enrolledImageOverlay != null) {
          binding.enrolledImageOverlay.setVisibility(View.GONE);
          binding.enrolledImageOverlay.setImageBitmap(null);
        }
      });
    }

    //synchronized (this) {
    //  if (deviceAuthHelper != null) {
    //    deviceAuthHelper.delete();
    //    deviceAuthHelper = null;
    //  }
    //  if (hostAuthHelper != null) {
    //    hostAuthHelper.delete();
    //    hostAuthHelper = null;
    //  }
    //}
    enableButtons();
  }

  @Override
  public void onAuthFailure(@NonNull String status, @NonNull List<Rect> faceRects) {
    if (isFragmentAttached() && uiFeedbackHelper != null) {
      var sharedViewModel = new ViewModelProvider(requireActivity()).get(RealSenseIdSharedViewModel.class);
      uiFeedbackHelper.onAuthFailure(status,
                                     faceRects,
                                     sharedViewModel.getCurrentCameraRotation(),
                                     getScaleFactor());
    }

    //synchronized (this) {
    //  if (deviceAuthHelper != null) {
    //    deviceAuthHelper.delete();
    //    deviceAuthHelper = null;
    //  }
    //  if (hostAuthHelper != null) {
    //    hostAuthHelper.delete();
    //    hostAuthHelper = null;
    //  }
    //}
    enableButtons();
  }

  @Override
  public void onEnrollmentSuccess(String name) {
    Timber.d("PreviewFragment.onEnrollmentSuccess(%s) on thread=%s", name, Thread.currentThread().getName());
    if (isFragmentAttached() && uiFeedbackHelper != null) {
      uiFeedbackHelper.onEnrollmentSuccess(name);
    }
    if (userListViewModel != null) {
      userListViewModel.markDeviceUsersStale();
    }
    synchronized (this) {
      deviceEnrollmentHelper = null;
      hostEnrollmentHelper = null;
    }
    enableButtons();
  }

  @Override
  public void onEnrollmentFailure(String status) {
    if (isFragmentAttached() && uiFeedbackHelper != null) {
      uiFeedbackHelper.onEnrollmentFailure(status);
    }
    synchronized (this) {
      deviceEnrollmentHelper = null;
      hostEnrollmentHelper = null;
    }
    enableButtons();
  }

  public void showHint(String hint) {
    if (isFragmentAttached() && uiFeedbackHelper != null) {
      uiFeedbackHelper.showHint(hint);
    }
  }

  @Override
  public void onDestroyView() {
    Timber.d("PreviewFragment onDestroyView");

    // Unregister device attachment callback
    if (deviceAttachmentCallback != null) {
      try {
        var sharedViewModel = new ViewModelProvider(requireActivity())
          .get(RealSenseIdSharedViewModel.class);
        sharedViewModel.unregisterDeviceAttachmentCallback(deviceAttachmentCallback);
      }
      catch (Exception e) {
        Timber.w(e, "Error unregistering device attachment callback");
      }
    }

    // Cleanup UI feedback helper first
    if (uiFeedbackHelper != null) {
      try {
        uiFeedbackHelper.cleanup();
      }
      catch (Exception e) {
        Timber.w(e, "Error cleaning up UI feedback helper");
      }
      finally {
        uiFeedbackHelper = null;
      }
    }

    // Cancel compatibility checker
    if (compatibilityChecker != null) {
      try {
        compatibilityChecker.destroy();
      }
      catch (Exception e) {
        Timber.w(e, "Error destroying compatibility checker");
      }
      finally {
        compatibilityChecker = null;
      }
    }

    // Clear lazy-initialized helpers
    synchronized (this) {
      if (hostEnrollmentHelper != null) {
        hostEnrollmentHelper.destroy();
      }
      deviceEnrollmentHelper = null;
      hostEnrollmentHelper = null;
      hostAuthHelper = null;
      deviceAuthHelper = null;
    }

    // Clear binding
    binding = null;

    // Clear window flags
    try {
      requireActivity().getWindow().clearFlags(
        WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
    catch (Exception e) {
      Timber.w(e, "Error clearing window flags");
    }

    // Call parent last
    super.onDestroyView();
  }

  @Override
  public void onDestroy() {
    if (nonNull(authenticator)) {
      try {
        authenticator.Disconnect();
      }
      catch (Exception ignored) {
      }
      finally {
        authenticator.delete();
      }
    }

    if (taskExecutor != null) {
      taskExecutor.shutdown();
      try {
        if (!taskExecutor.awaitTermination(
          5, java.util.concurrent.TimeUnit.SECONDS)) {
          taskExecutor.shutdownNow();
        }
      }
      catch (InterruptedException e) {
        taskExecutor.shutdownNow();
        Thread.currentThread().interrupt();
      }
    }

    super.onDestroy();
  }

  private String getAuthButtonText(DeviceConfig.AlgoFlow mode) {
    if (mode == DeviceConfig.AlgoFlow.All) {
      return "Authenticate";
    }
    else if (mode == DeviceConfig.AlgoFlow.FaceDetectionOnly) {
      return "Detect Face";
    }
    else if (mode == DeviceConfig.AlgoFlow.SpoofOnly) {
      return "Detect Spoof";
    }
    else if (mode == DeviceConfig.AlgoFlow.RecognitionOnly) {
      return "Recognize Face";
    }
    else {
      return "Authenticate";
    }
  }

  /**
   * Callback method that handles UI updates when device attachment status
   * changes
   */
  private void onDeviceAttachmentChanged() {
    if (!isFragmentAttached() || binding == null) {
      Timber.d("onDeviceAttachmentChanged: Fragment not attached or binding is null");
      return;
    }

    // Ensure this runs on UI thread
    requireActivity().runOnUiThread(() -> {
      if (!isFragmentAttached() || binding == null) {
        Timber.d("onDeviceAttachmentChanged: Fragment not attached or binding is null (UI thread check)");
        return;
      }

      var sharedViewModel = new ViewModelProvider(requireActivity())
        .get(RealSenseIdSharedViewModel.class);

      var deviceAttached = sharedViewModel.areDeviceSettingsLoaded();
      Timber.d("onDeviceAttachmentChanged: deviceAttached = %s", deviceAttached);

      if (deviceAttached) {
        // Re-run compatibility check now that device is fully connected
        refreshCompatibilityCheck();

        // Start preview now that USB permission is granted
        DeviceType devType = sharedViewModel.getCurrentDeviceType();
        if (devType != DeviceType.Unknown) {
          initializePreview(devType, PreviewFragment.this);
        }

        var mode = sharedViewModel.getOperationMode().getValue();
        Timber.d("onDeviceAttachmentChanged: operation mode = %s", mode);

        if (mode != null) {
          Timber.d("DEBUG: Operation mode is %s", mode.toString());
          binding.buttonAuthenticate.setText(getAuthButtonText(mode));

          // Only update visibility if NOT in one-to-one mode
          // One-to-one mode observer handles visibility when enabled
          boolean oneToOneMode = sharedViewModel.isOneToOneModeEnabled();
          if (!oneToOneMode) {
            binding.buttonEnroll.setVisibility(getEnrollButtonVisibility(mode));
            binding.buttonAuthenticate.setVisibility(View.VISIBLE);
          }

          Timber.d("onDeviceAttachmentChanged: Updated UI - auth text: %s, one-to-one mode: %s",
                   getAuthButtonText(mode), oneToOneMode);
        }
        else {
          Timber.w("onDeviceAttachmentChanged: Device attached but operation mode is null");
          // Only show default state if NOT in one-to-one mode
          boolean oneToOneMode = sharedViewModel.isOneToOneModeEnabled();
          if (!oneToOneMode) {
            binding.buttonAuthenticate.setText("Authenticate");
            binding.buttonAuthenticate.setVisibility(View.VISIBLE);
            binding.buttonEnroll.setVisibility(View.VISIBLE);
          }
        }
      }
      else {
        Timber.d("onDeviceAttachmentChanged: No device attached, hiding buttons");
        sharedViewModel.setDeviceCompatible(false);
        boolean oneToOneMode = sharedViewModel.isOneToOneModeEnabled();
        if (!oneToOneMode) {
          binding.buttonAuthenticate.setVisibility(View.GONE);
          binding.buttonEnroll.setVisibility(View.GONE);
        }
      }
    });
  }

  @Override
  public void onAuthHint(@NonNull String hint, float frameScore) {
    Timber.d("onAuthHint: %s (frame score: %.2f)", hint, frameScore);
    if (isFragmentAttached() && uiFeedbackHelper != null) {
      uiFeedbackHelper.showHint(hint);
    }
  }

  @Override
  public void onEnrollmentHint(String hint) {
    if (isFragmentAttached() && uiFeedbackHelper != null) {
      uiFeedbackHelper.showHint(hint);
    }
  }

  // Constants
  private static final class Constants {
    static final int MAX_IMAGE_SIZE_BYTES = 900 * 1024;

    static final String DEFAULT_PNG_URL =
      "https://user-images.githubusercontent.com/17826183/190878147-72262a44-4d27-4ee0-ae83-a6eb25440f0b.png";
    static final String DEFAULT_FILENAME = "test.png";
  }
}

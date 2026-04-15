package com.realsenseai.rsid.sample.ui.preview;

import static androidx.core.util.ObjectsCompat.requireNonNull;

import android.content.IntentFilter;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.RectF;
import android.graphics.SurfaceTexture;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ProgressBar;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import com.realsenseai.rsid.api.DeviceConfig;
import com.realsenseai.rsid.api.DeviceType;
import com.realsenseai.rsid.api.Preview;
import com.realsenseai.rsid.api.PreviewConfig;
import com.realsenseai.rsid.api.PreviewMode;
import com.realsenseai.rsid.api.SerialConfig;
import com.realsenseai.rsid.sample.R;
import com.realsenseai.rsid.sample.callbacks.RealSenseIdPreviewCallback;
import com.realsenseai.rsid.sample.ui.shared.RealSenseIdSharedViewModel;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import com.realsenseai.rsid.sample.util.UsbDevicesReceiver;
import com.realsenseai.rsid.sample.util.preview.PreviewHelper;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import timber.log.Timber;

public abstract class BaseRealSenseIdPreviewFragment extends Fragment
  implements RealSenseIdPreviewCallback, UsbDevicesReceiver.UsbListener {

  private final Handler mainHandler = new Handler(Looper.getMainLooper());
  private final AtomicBoolean isDestroyed = new AtomicBoolean(false);
  private final AtomicBoolean isProcessingFrame = new AtomicBoolean(false);
  private final AtomicLong frameCounter = new AtomicLong(0);
  private final AtomicBoolean isPreviewActive = new AtomicBoolean(false);
  private final Object surfaceLock = new Object();
  private UsbDevicesReceiver usbReceiver;
  private View rootView;
  private PreviewViewModel previewViewModel;
  private Preview preview;
  private PreviewHelper previewHelper;
  private long lastFrameTime = 0;

  // FPS sampling variables
  private long fpsStartTime = 0;
  private int fpsFrameCount = 0;
  private boolean isSamplingFps = false;
  private Surface cachedSurface;

  // Scale factor for resizing TextureView
  private float scaleFactor = -100.0f;
  private RealSenseIdSharedViewModel sharedViewModel;

  private volatile boolean isSurfaceDestroyed = false;

  @Nullable
  protected abstract TextureView getVideoView();

  @Nullable
  protected abstract ProgressBar getLoadingIndicator();

  protected abstract void refreshCompatibilityCheck();

  @Override
  public void onCreate(@Nullable Bundle savedInstanceState) {

    super.onCreate(savedInstanceState);
    sharedViewModel = new ViewModelProvider(requireActivity())
      .get(RealSenseIdSharedViewModel.class);
  }

  // Simplified preview initialization - no threading complications
  public void initializePreview(DeviceType deviceType, RealSenseIdPreviewCallback callback) {
    Timber.d("initializePreview...");

    if (isDestroyed.get()) {
      Timber.d("Fragment destroyed, skipping preview init");
      return;
    }

    if (isPreviewActive.get()) {
      Timber.d("Preview already active, skipping");
      return;
    }

    if (!isFragmentAttached()) {
      Timber.d("Fragment not attached, skipping preview init");
      return;
    }

    showCameraIcon();

    SerialConfig config = getSerialConfig();
    if (config == null) {
      Timber.e("[1] Failed to get serial config");
      return;
    }

    try {
      PreviewConfig previewConfig = createPreviewConfig(config);
      previewConfig.setDeviceType(deviceType);
      preview = new Preview(previewConfig);
      previewHelper = new PreviewHelper(callback, sharedViewModel);

      boolean success;
      try {
        success = preview.StartPreview(previewHelper);
      }
      catch (Exception e) {
        Timber.e(e, "Error starting preview");
        success = false;
      }
      finally {
        SDKWrapper.INSTANCE.closeConnection();
      }

      isPreviewActive.set(success);

      if (success) {
        Timber.d("Preview started successfully!");
        frameCounter.set(0);                           // Reset frame counter
        isSamplingFps = !isFullFrameDumpModeEnabled(); // Disable FPS sampling
        // for Full Frame mode
        if (isSamplingFps) {
          startFpsSampling();
        }
        else {
          Timber.i("FPS monitoring disabled - Full Frame dump mode active");
        }
      }
      else {
        Timber.e("Preview failed to start");
        isSamplingFps = false;
        cleanupPreview();
      }
    }
    catch (Exception e) {
      Timber.e(e, "Exception during preview initialization");
      cleanupPreview();
    }
  }

  private boolean isFullFrameDumpModeEnabled() {
    DeviceConfig.DumpMode currentMode = sharedViewModel.getCurrentDumpMode();
    return currentMode == DeviceConfig.DumpMode.FullFrame;
  }

  private void showCameraIcon() {
    // Display icon while camera is initializing
    TextureView textureView = getVideoView();
    if (textureView != null) {
      try {
        Bitmap icon = BitmapFactory.decodeResource(requireContext().getResources(),
                                                   R.drawable.f455);
        if (icon != null) {
          mainHandler.post(() -> drawBitmapToTextureView(textureView, icon));
        }
      }
      catch (Exception e) {
        Timber.e(e, "Error displaying initialization icon");
      }
    }

    ProgressBar progressBar = getLoadingIndicator();
    if (progressBar != null && !isFullFrameDumpModeEnabled()) {
      progressBar.setVisibility(View.VISIBLE);
    }
    else if (progressBar != null) {
      Timber.d("Full frame dump is enabled, hiding loading indicator");
      progressBar.setVisibility(View.INVISIBLE);
    }

    scaleFactor = -100.0f; // Reset scale factor
  }

  public void stopPreview() {
    Timber.d("stopPreview...");
    isSamplingFps = false;

    if (!isPreviewActive.get()) {
      Timber.d("Preview not active, skipping...");
      return;
    }
    else {
      Timber.d("Preview is active, stopping...");
    }

    isPreviewActive.set(false);

    cleanupPreviewInternal();

    if (!isDestroyed.get()) {
      try {
        SDKWrapper.INSTANCE.closeConnection();
      }
      catch (Exception e) {
        Timber.e(e, "Error during reconnect");
      }
    }
  }

  private void cleanupPreviewInternal() {
    if (preview != null) {
      try {
        preview.StopPreview();
      }
      catch (Exception e) {
        Timber.e(e, "Error stopping preview");
      }
    }
    cleanupPreview();
  }

  private void cleanupPreview() {
    preview = null;
    if (previewHelper != null) {
      try {
        previewHelper.release();
      }
      catch (Exception e) {
        Timber.e(e, "Error releasing preview helper");
      }
      finally {
        previewHelper = null;
      }
    }
  }

  @Nullable
  @Override
  public View onCreateView(@NonNull LayoutInflater layoutInflater,
                           ViewGroup container, Bundle savedInstanceState) {
    Timber.d("onCreateView...");
    super.onCreateView(layoutInflater, container, savedInstanceState);

    // Reset destroyed flag when creating view
    isDestroyed.set(false);
    showCameraIcon();

    previewViewModel = new ViewModelProvider(this).get(PreviewViewModel.class);
    rootView = getRootView(layoutInflater, container);
    return rootView;
  }

  @Override
  public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
    Timber.d("onViewCreated...");
    super.onViewCreated(view, savedInstanceState);

    if (!isDestroyed.get()) {
      observeViewModelStates();

      // Initialize TextureView with proper listener
      TextureView textureView = getVideoView();
      if (textureView != null) {
        textureView.setSurfaceTextureListener(new TextureView.SurfaceTextureListener() {
          @Override
          public void onSurfaceTextureAvailable(@NonNull SurfaceTexture surface, int width, int height) {
            Timber.d("SurfaceTexture available: %dx%d", width, height);
            synchronized (surfaceLock) {
              isSurfaceDestroyed = false;
            }
          }

          @Override
          public void onSurfaceTextureSizeChanged(@NonNull SurfaceTexture surface, int width, int height) {
            Timber.d("SurfaceTexture size changed: %dx%d", width, height);
          }

          @Override
          public boolean onSurfaceTextureDestroyed(@NonNull SurfaceTexture surface) {
            Timber.d("SurfaceTexture destroyed");
            synchronized (surfaceLock) {
              isSurfaceDestroyed = true;
              if (cachedSurface != null) {
                cachedSurface.release();
                cachedSurface = null;
              }
            }
            return true;
          }

          @Override
          public void onSurfaceTextureUpdated(@NonNull SurfaceTexture surface) {
            // Optional: Handle surface updates
          }
        });

        Timber.d("TextureView initialized with listener");
      }
    }
  }

  @Override
  public void onResume() {
    Timber.d("onResume...");
    super.onResume();

    // Reset destroyed flag when resuming
    isDestroyed.set(false);

    // Display waiting image..
    showCameraIcon();

    try {
      usbReceiver = new UsbDevicesReceiver(this);
      IntentFilter filter = new IntentFilter();
      filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
      filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
      requireContext().registerReceiver(usbReceiver, filter);

      // Longer delay to ensure everything is ready and avoid race conditions
      mainHandler.postDelayed(() -> {
        if (isFragmentAttached() && !isDestroyed.get() && usbReceiver != null &&
            usbReceiver.isRealSenseIdDeviceAttached(requireContext())) {
          DeviceType deviceType = sharedViewModel.getDeviceType().getValue();
          if (deviceType != null) {
            initializePreview(deviceType, this);
          }
          else {
            Timber.e("Device not supported");
          }
        }
      }, 500);
    }
    catch (Exception e) {
      Timber.e(e, "Error in onResume");
    }
  }

  @Override
  public void onPreviewBitmapReady(@NonNull Bitmap bitmap) {
    boolean isFullFrameMode = isFullFrameDumpModeEnabled();

    if (isSamplingFps) {
      fpsFrameCount++;
    }

    // In Full Frame dump mode, we don't need to display the preview
    // The camera is connected and ready for snapshots during authentication
    if (isFullFrameMode) {
      Timber.d(
        "Full Frame dump mode - frame received in onPreviewBitmapReady but not displayed (camera ready for snapshots)");
      recycleBitmap(bitmap);
      return;
    }

    TextureView textureView = getVideoView();

    // Add debugging for condition checks
    boolean fragmentAttached = isFragmentAttached();
    boolean notDestroyed = !isDestroyed.get();
    boolean previewActive = isPreviewActive.get();
    boolean textureViewExists = textureView != null;

    // For normal preview mode, check all conditions
    if (!fragmentAttached || !notDestroyed || !previewActive ||
        !textureViewExists) {
      recycleBitmap(bitmap);
      return;
    }

    long currentTime = System.currentTimeMillis();
    long frameCount = frameCounter.incrementAndGet();

    if (currentTime - lastFrameTime < Constants.FRAME_INTERVAL_MS) {
      recycleBitmap(bitmap);
      return;
    }
    lastFrameTime = currentTime;

    // Skip if already processing
    if (!isProcessingFrame.compareAndSet(false, true)) {
      recycleBitmap(bitmap);
      return;
    }

    try {
      ProgressBar progressBar = getLoadingIndicator();
      if (progressBar != null) {
        progressBar.setVisibility(View.GONE);
      }

      mainHandler.post(() -> drawBitmapToTextureView(textureView, bitmap));
    }
    catch (Exception e) {
      Timber.e(e, "Error processing bitmap on main thread");
      recycleBitmap(bitmap);
    }
    finally {
      isProcessingFrame.set(false);
    }
  }

  protected float getScaleFactor() {
    return scaleFactor;
  }

  // Modify the drawBitmapToTextureView method
  private void drawBitmapToTextureView(TextureView textureView, Bitmap bitmap) {
    if (textureView == null || bitmap == null || bitmap.isRecycled()) {
      recycleBitmap(bitmap);
      return;
    }

    // Ensure we're on the main thread
    if (Looper.myLooper() != Looper.getMainLooper()) {
      mainHandler.post(() -> drawBitmapToTextureView(textureView, bitmap));
      return;
    }

    synchronized (surfaceLock) {
      if (isSurfaceDestroyed || isDestroyed.get()) {
        recycleBitmap(bitmap);
        return;
      }

      resizeTextureViewToImage(textureView, bitmap);

      try {
        SurfaceTexture surfaceTexture = textureView.getSurfaceTexture();
        if (surfaceTexture == null) {
          recycleBitmap(bitmap);
          return;
        }

        // Check if surface is still valid before creating/using it
        if (cachedSurface == null || !cachedSurface.isValid()) {
          if (cachedSurface != null) {
            cachedSurface.release();
          }
          cachedSurface = new Surface(surfaceTexture);
        }

        // Verify surface is still valid before locking
        if (!cachedSurface.isValid()) {
          recycleBitmap(bitmap);
          return;
        }

        Canvas canvas = null;
        try {
          canvas = cachedSurface.lockCanvas(null);
          if (canvas != null) {
            canvas.drawColor(Color.LTGRAY);
            canvas.save();
            canvas.scale(-1f, 1f, canvas.getWidth() / 2f, canvas.getHeight() / 2f);

            RectF destRect = new RectF(0, 0, canvas.getWidth(), canvas.getHeight());
            canvas.drawBitmap(bitmap, null, destRect, null);

            canvas.restore();
          }
        }
        finally {
          if (canvas != null && cachedSurface.isValid()) {
            try {
              cachedSurface.unlockCanvasAndPost(canvas);
            }
            catch (Exception e) {
              Timber.e(e, "Error unlocking canvas");
            }
          }
        }
      }
      catch (Exception e) {
        Timber.e(e, "Error drawing bitmap to TextureView");
      }
      finally {
        recycleBitmap(bitmap);
      }
    }
  }

  /**
   * Resize TextureView to fit the given bitmap while maintaining aspect ratio.
   * This ensures the preview is displayed correctly without distortion.
   */
  private void resizeTextureViewToImage(TextureView textureView, Bitmap bitmap) {
    if (textureView == null || bitmap == null) return;

    // Get parent dimensions
    ViewGroup parent = (ViewGroup)textureView.getParent();
    int parentWidth = parent.getWidth();
    int parentHeight = parent.getHeight();

    // Calculate scaled size
    float scale = Math.min(
      (float)parentWidth / bitmap.getWidth(),
      (float)parentHeight / bitmap.getHeight()
    );

    if (Math.abs(scaleFactor - scale) < 0.000001f) {
      return;
    }

    scaleFactor = scale; // Store scale factor for future use

    int newWidth = (int)(bitmap.getWidth() * scale);
    int newHeight = (int)(bitmap.getHeight() * scale);

    Timber.d("Resizing TextureView to %dx%d (scale: %.2f)", newWidth, newHeight, scale);

    // Apply new size
    ViewGroup.LayoutParams params = textureView.getLayoutParams();
    params.width = newWidth;
    params.height = newHeight;
    textureView.setLayoutParams(params);

    // Resize the overlay view to the new size
    View overlayView = parent.findViewWithTag("video_overlay");
    if (overlayView != null) {
      ViewGroup.LayoutParams overlayParams = overlayView.getLayoutParams();
      overlayParams.width = newWidth;
      overlayParams.height = newHeight;
      overlayView.setLayoutParams(overlayParams);
    }
  }

  @Override
  public void onPause() {
    Timber.d("onPause...");
    super.onPause();

    // Remove any pending callbacks
    mainHandler.removeCallbacksAndMessages(null);

    // Unregister receiver
    if (usbReceiver != null) {
      try {
        requireContext().unregisterReceiver(usbReceiver);
      }
      catch (Exception e) {
        Timber.e(e, "Error unregistering USB receiver");
      }
      finally {
        usbReceiver = null;
      }
    }

    // Stop preview
    stopPreview();

    // Let this be the last frame to appear so we can restore with it.
    showCameraIcon();

    SDKWrapper.INSTANCE.closeUVCConnection();
    SDKWrapper.INSTANCE.closeConnection();

    // Clear root view reference
    rootView = null;
  }

  private void startFpsSampling() {
    // Skip FPS monitoring completely in Full Frame dump mode
    if (isFullFrameDumpModeEnabled()) {
      Timber.i("Skipping FPS monitoring - Full Frame dump mode is active");
      return;
    }

    if (isPreviewActive.get()) {
      fpsStartTime = System.currentTimeMillis();
      fpsFrameCount = 0;

      // Stop sampling after 10 seconds
      mainHandler.postDelayed(() -> {
        if (isSamplingFps) {
          long duration = System.currentTimeMillis() - fpsStartTime;
          float fps = (fpsFrameCount * 1000f) / duration;
          Timber.d("Calculated FPS over 10 seconds: %.1f", fps);
          if (fps < 0.1) {  // No frames for 10 seconds!
            Timber.i(SDKWrapper.INSTANCE.getDiagnosticInfo());
            Timber.w("FPS is zero, restarting camera!");
            try {
              requireNonNull(SDKWrapper.INSTANCE.getDeviceController()).Reboot();
            }
            catch (Exception e) {
              Timber.e(e, "Error rebooting device");
            }
          }
          else {
            startFpsSampling();
          }
        }
      }, 10000);
    }
  }

  @Override
  public void onDestroyView() {
    Timber.d("onDestroyView");

    // Set destroyed flag
    isDestroyed.set(true);

    // Remove all pending handler callbacks
    mainHandler.removeCallbacksAndMessages(null);

    // Stop preview before other cleanup
    stopPreview();

    // Clear root view reference
    rootView = null;

    // Clean up Texture
    cleanupTextureView();

    super.onDestroyView();
  }

  private void cleanupTextureView() {
    synchronized (surfaceLock) {
      isSurfaceDestroyed = true;

      try {
        if (cachedSurface != null) {
          if (cachedSurface.isValid()) {
            cachedSurface.release();
          }
          cachedSurface = null;
        }

        TextureView textureView = getVideoView();
        if (textureView != null) {
          textureView.setSurfaceTextureListener(null);
        }
      }
      catch (Exception e) {
        Timber.e(e, "Error cleaning up TextureView");
      }
    }
  }

  @Override
  public void onDestroy() {
    Timber.d("onDestroy");

    isDestroyed.set(true);
    previewViewModel = null;

    try {
      SDKWrapper.INSTANCE.closeConnection();
    }
    catch (Exception e) {
      Timber.e(e, "Error closing SDK connection in onDestroy");
    }

    super.onDestroy();
  }

  @Override
  public void onDeviceDetached(@Nullable UsbDevice usbDevice) {
    Timber.d("USB device detached");
    // TODO: Force compatibility check revisit?

    if (!isDestroyed.get()) {
      stopPreview();
    }
  }

  @Override
  public void onDeviceAttached(@Nullable UsbDevice usbDevice) {
    Timber.d("USB device attached: %s", usbDevice);
    // Don't start compatibility check or preview here — USB permission may not
    // be granted yet. These are triggered later by onDeviceAttachmentChanged
    // once permission is obtained and device settings are loaded.
  }

  private SerialConfig getSerialConfig() {
    try {
      SerialConfig config = SDKWrapper.INSTANCE.getCachedOrNewSerialConfig();
      if (config != null) {
        Timber.d("Serial config obtained successfully");
      }
      return config;
    }
    catch (Exception e) {
      Timber.e(e, "[2] Failed to get serial config");
      return null;
    }
  }

  private PreviewConfig createPreviewConfig(SerialConfig config) {
    boolean uvcConnectionOpened = SDKWrapper.INSTANCE.openUVCConnection();
    int uvcFileDescriptor = SDKWrapper.INSTANCE.getUVCFileDescriptor();

    PreviewConfig previewConfig = new PreviewConfig();
    // Set RAW10 - Only when Full Dump is enabled.
    if (isFullFrameDumpModeEnabled()) {
      Timber.d("Dump Mode is Full Frame, set preview mode to RAW10_1080P");
      previewConfig.setPreviewMode(PreviewMode.RAW10_1080P);
    }
    else {
      previewConfig.setPreviewMode(PreviewMode.MJPEG_1080P);
      Timber.d("Set preview mode to MJPEG mode");
    }
    previewConfig.setCameraNumber(uvcFileDescriptor);
    previewConfig.setPortraitMode(true);
    previewConfig.setRotateRaw(false);
    return previewConfig;
  }

  protected Boolean isFragmentAttached() {
    return !isDestroyed.get() && getActivity() != null && !requireActivity().isDestroyed() && isAdded() && !isDetached();
  }

  private void observeViewModelStates() {
    // Observe any LiveData or Flow from ViewModel
  }

  private void recycleBitmap(@Nullable Bitmap bitmap) {
    if (bitmap != null && !bitmap.isRecycled()) {
      try {
        // If we're already on main thread, recycle immediately
        if (Looper.myLooper() == Looper.getMainLooper()) {
          bitmap.recycle();
        }
        else {
          // Post to main thread for recycling
          mainHandler.post(() -> {
            try {
              if (!bitmap.isRecycled()) {
                bitmap.recycle();
              }
            }
            catch (Exception e) {
              Timber.e(e, "Error recycling bitmap on main thread");
            }
          });
        }
      }
      catch (Exception e) {
        Timber.e(e, "Error in bitmap recycling");
      }
    }
  }

  /**
   * Provides access to the PreviewHelper for subclasses to control session
   * management.
   *
   * @return The current PreviewHelper instance, or null if not initialized
   */
  protected PreviewHelper getPreviewHelper() { return previewHelper; }

  protected abstract View getRootView(@NonNull LayoutInflater layoutInflater,
                                      @NonNull ViewGroup container);

  private static final class Constants {
    static final long FRAME_INTERVAL_MS = 50; // ~20 FPS to reduce load
  }
}

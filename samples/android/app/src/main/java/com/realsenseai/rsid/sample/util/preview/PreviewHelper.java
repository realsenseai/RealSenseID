package com.realsenseai.rsid.sample.util.preview;

import android.graphics.Bitmap;
import android.os.Handler;
import android.os.Looper;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.realsenseai.rsid.api.DeviceConfig;
import com.realsenseai.rsid.api.Image;
import com.realsenseai.rsid.api.PreviewImageReadyCallback;
import com.realsenseai.rsid.sample.callbacks.RealSenseIdPreviewCallback;
import com.realsenseai.rsid.sample.ui.shared.RealSenseIdSharedViewModel;
import com.realsenseai.rsid.sample.util.FrameDumper;
import java.lang.ref.WeakReference;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import timber.log.Timber;

public class PreviewHelper extends PreviewImageReadyCallback {
  private final String TAG = "PreviewHelper";
  private final WeakReference<RealSenseIdPreviewCallback> callbackRef;
  private final ExecutorService backgroundExecutor;
  private final Handler mainHandler;
  private final RealSenseIdSharedViewModel sharedViewModel;
  private FrameDumper frameDumper;
  private volatile boolean isActive = true;
  private String currentSessionType =
    "AUTH"; // Default to AUTH, changes to REG for enrollment

  public PreviewHelper(@NonNull RealSenseIdPreviewCallback callback,
                       @NonNull RealSenseIdSharedViewModel sharedViewModel) {
    this.callbackRef = new WeakReference<>(callback);
    this.sharedViewModel = sharedViewModel;
    this.backgroundExecutor = Executors.newSingleThreadExecutor(r -> {
      Thread t = new Thread(r, "PreviewHelper-Background");
      t.setPriority(Thread.NORM_PRIORITY -
                    1); // Lower priority to avoid blocking UI
      return t;
    });
    this.mainHandler = new Handler(Looper.getMainLooper());

    // Initialize FrameDumper if Full Frame dump mode is enabled
    if (isFullFrameDumpModeEnabled()) {
      initializeFrameDumper();
    }
  }

  /**
   * Check if dump mode is set to full frame
   */
  public boolean isFullFrameDumpModeEnabled() {
    return sharedViewModel.getCurrentDumpMode() ==
           DeviceConfig.DumpMode.FullFrame;
  }

  /**
   * Initialize FrameDumper for Full Frame dump mode
   */
  private void initializeFrameDumper() {
    Timber.tag(TAG).i("Initializing FrameDumper for Full Frame dump mode");

    try {
      frameDumper = new FrameDumper(); // FrameDumper handles directory creation
      // internally
      Timber.tag(TAG).i("FrameDumper initialized successfully");
    }
    catch (Exception e) {
      Timber.tag(TAG).e(e, "Failed to initialize FrameDumper");
      frameDumper = null;
    }
  }

  /**
   * Starts a new authentication session for frame dumping.
   * Call this method when starting a new authentication operation.
   */
  public void startNewAuthenticationSession() {
    currentSessionType = "AUTH";
    if (frameDumper != null) {
      frameDumper.createNewSessionDirectory("Authenticate", null);
    }
  }

  /**
   * Starts a new enrollment session for frame dumping.
   * Call this method when starting a new enrollment operation.
   *
   * @param enrollId The ID being enrolled
   */
  public void startNewEnrollmentSession(String enrollId) {
    currentSessionType = "REG";
    if (frameDumper != null) {
      frameDumper.createNewSessionDirectory("Enroll", enrollId);
    }
  }

  /*
  public static int[] convertByteBufferToARGB(ByteBuffer buffer, int width, int
  height, int stride) { int[] pixels = new int[width * height]; IntBuffer
  intBuffer = buffer.asIntBuffer();

    for (int row = 0; row < height; row++) {
      int rowOffset = row * stride / 4;
      int pixelOffset = row * width;

      for (int col = 0; col < width; col++) {
        pixels[pixelOffset + col] = intBuffer.get(rowOffset + col);
      }
    }
    return pixels;
  }
  */

  @Override
  public void OnPreviewImageReady(@Nullable Image image) {
    if (!isActive) {
      if (image != null) {
        image.delete();
      }
      return;
    }

    RealSenseIdPreviewCallback callback = callbackRef.get();
    if (callback == null) {
      Timber.w("Callback no longer available");
      return;
    }

    if (image == null) {
      Timber.w("OnPreviewImageReady: Image is null.");
      return;
    }

    var pix = image.GetBitmapPixels();
    int width = (int)image.getWidth();
    int height = (int)image.getHeight();

    // Process image on background thread to avoid blocking SDK callback thread
    backgroundExecutor.execute(() -> {
      if (!isActive) {
        return;
      }

      try {
        Bitmap bitmap =
          Bitmap.createBitmap(pix, width, height, Bitmap.Config.ARGB_8888);

        // Switch to UI thread for callback
        mainHandler.post(() -> {
          if (isActive) {
            RealSenseIdPreviewCallback cb = callbackRef.get();
            if (cb != null) {
              cb.onPreviewBitmapReady(bitmap);
            }
            else {
              // Clean up bitmap if callback is no longer available
              if (!bitmap.isRecycled()) {
                bitmap.recycle();
              }
            }
          }
          else {
            // Clean up bitmap if helper is no longer active
            if (!bitmap.isRecycled()) {
              bitmap.recycle();
            }
          }
        });
      }
      catch (Exception e) {
        Timber.e(e, "Error processing preview image");
        // Post error to UI thread if needed
        mainHandler.post(
          ()
            -> {
            // You could notify about the error here if needed
          });
      }
    });
  }

  // Slow Java example working with the byte buffer
  // int width = (int) image.getWidth();
  // int height = (int) image.getHeight();
  // int stride = (int) image.getStride(); // Total bytes per row
  // byte[] bufferArray = image.GetImageBuffer();
  //// Create an array to hold ARGB pixels
  // int[] argbArray = new int[width * height];
  //
  // int totalPixels = width * height;
  // int alpha = 0xFF << 24; // Precompute alpha for ARGB
  //

  /// / Extract and convert BGR to ARGB
  // for (int i = 0; i < totalPixels; i++) {
  //   int row = i / width;
  //   int col = i % width;
  //
  //   int bIndex = row * stride + (col * 3); // Blue component index
  //   int gIndex = bIndex + 1;                 // Green component index
  //   int rIndex = bIndex + 2;                 // Red component index
  //
  //   // Read individual BGR components from the buffer array
  //   int blue = bufferArray[bIndex] & 0xFF; // Convert to unsigned
  //   int green = bufferArray[gIndex] & 0xFF;
  //   int red = bufferArray[rIndex] & 0xFF;
  //
  //   // Combine into ARGB format (A=255 for full opacity)
  //   argbArray[i] = alpha | (red << 16) | (green << 8) | blue;
  // }
  @Override
  public void OnSnapshotImageReady(Image image) {
    Timber.tag(TAG).d("OnSnapshotImageReady");
    if ((int)image.getMetadata().getLed() <= 0) {
      Timber.tag(TAG).d("OnSnapshotImageReady - led off.");
      // ToDo YARDEN - show in preview the snapshot every 5 sec like the
      // rsid-viewer
      return;
    }

    if (!isActive) {
      if (image != null) {
        image.delete();
      }
      return;
    }

    if (image == null) {
      Timber.w("OnSnapshotImageReady: Image is null.");
      return;
    }

    // Only dump if we're in Full Frame dump mode
    if (!isFullFrameDumpModeEnabled()) {
      return;
    }

    var md = image.getMetadata();

    // if (md != null) {
    //   Timber.tag(TAG).d("OnSnapshotImageReady() Frame: %d, TIME = %d, W = %d,
    //   H = %d, EXPOSURE = %d, LED = %d, SENSOR = %s",
    //                     image.getNumber(), md != null ? md.getTimestamp() :
    //                     -1, image.getWidth(), image.getHeight(),
    //                     (int)md.getExposure(), (int)md.getLed(),
    //                     md.getSensor_id() != 0 ? "RIGHT" : "LEFT");
    // }

    // Create immediate buffer copy and capture metadata on SDK callback thread
    byte[] immediateBufferCopy = null;

    // Capture all metadata immediately to avoid accessing Image object later
    final long imageSize = image.getSize();
    final long timestamp =
      md != null ? md.getTimestamp() : System.currentTimeMillis();
    final int exposure = md != null ? (int)md.getExposure() : 0;
    final int led = md != null ? (int)md.getLed() : 0;
    final long gain = md != null ? md.getGain() : 0;
    final long status = md != null ? md.getStatus() : 0;
    final String sensor =
      (md != null && md.getSensor_id() != 0) ? "right" : "left";

    try {
      byte[] originalBuffer = image.GetImageBuffer();
      if (originalBuffer != null) {
        immediateBufferCopy = new byte[originalBuffer.length];
        System.arraycopy(originalBuffer, 0, immediateBufferCopy, 0,
                         originalBuffer.length);
      }
    }
    catch (Exception e) {
      Timber.tag(TAG).e(e, "Failed to create immediate buffer copy");
    }

    // Move heavy dumping operations to background thread
    final byte[] finalBufferCopy = immediateBufferCopy;
    backgroundExecutor.execute(() -> {
      if (!isActive || finalBufferCopy == null) {
        return;
      }

      // Use the existing FrameDumper
      if (frameDumper != null && frameDumper.isReady()) {
        frameDumper.dumpRawImageWithCapturedMetadata(
          finalBufferCopy, currentSessionType, timestamp, exposure, gain, led,
          sensor, status, imageSize);
      }
      else {
        if (frameDumper == null) {
          Timber.tag(TAG).w(
            "FrameDumper is null - Full Frame dump mode may not be enabled");
        }
        else {
          Timber.w("FrameDumper is not ready for dumping");
        }
      }
    });
  }

  public void release() {
    Timber.tag(TAG).w("PreviewHelper:: release");

    isActive = false;
    callbackRef.clear();

    // Shutdown background executor
    backgroundExecutor.shutdown();
    try {
      if (!backgroundExecutor.awaitTermination(
        1000, java.util.concurrent.TimeUnit.MILLISECONDS)) {
        backgroundExecutor.shutdownNow();
      }
    }
    catch (InterruptedException e) {
      backgroundExecutor.shutdownNow();
      Thread.currentThread().interrupt();
    }
  }
}

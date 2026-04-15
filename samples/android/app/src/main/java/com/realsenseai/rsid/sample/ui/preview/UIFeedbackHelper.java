// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.preview;

import static com.realsenseai.rsid.api.DeviceConfig.CameraRotation.Rotation_180_Deg;
import static com.realsenseai.rsid.api.DeviceConfig.CameraRotation.Rotation_270_Deg;
import static com.realsenseai.rsid.api.DeviceConfig.CameraRotation.Rotation_90_Deg;
import static java.util.Objects.nonNull;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.view.View;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.realsenseai.rsid.api.DeviceConfig;
import com.realsenseai.rsid.api.PersonRect;
import com.realsenseai.rsid.sample.databinding.FragmentPreviewBinding;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;
import timber.log.Timber;

/**
 * Helper class to manage UI feedback operations for the PreviewFragment.
 * Handles hint display, color management, and automatic reset functionality.
 */
public class UIFeedbackHelper {

  // Configuration
  private static final long HINT_RESET_DELAY_SECONDS = 4;
  // Dependencies
  private final Consumer<Runnable> uiThreadRunner;
  private final ScheduledExecutorService scheduledExecutor;
  private final List<Rect> faceRects;
  private final List<PersonRect> personRects;
  private final List<List<Point>> faceLandmarks;
  private final AtomicBoolean isDestroyed = new AtomicBoolean(false);
  private final Drawable overlayDrawable;
  // State
  private volatile FragmentPreviewBinding binding;
  private volatile ScheduledFuture<?> resetHintTask;

  /**
   * Constructor for UIFeedbackHelper
   *
   * @param uiThreadRunner    Consumer that executes code on UI thread (e.g., activity::runOnUiThread)
   * @param scheduledExecutor Executor for scheduling delayed tasks
   */
  public UIFeedbackHelper(@NonNull Consumer<Runnable> uiThreadRunner,
                          @NonNull ScheduledExecutorService scheduledExecutor) {
    this.uiThreadRunner = uiThreadRunner;
    this.scheduledExecutor = scheduledExecutor;
    this.faceRects = new ArrayList<>();
    this.personRects = new ArrayList<>();
    this.faceLandmarks = new ArrayList<>();
    overlayDrawable = new ColorDrawable();  // Placeholder initialization
  }

  /**
   * Sets the binding reference for UI operations
   */
  public void setBinding(@Nullable FragmentPreviewBinding binding) {
    this.binding = binding;
  }

  /**
   * Initialize the hint view with default state
   */
  public void initializeHint() {
    if (isDestroyed.get()) return;

    runOnUiThread(() -> {
      FragmentPreviewBinding currentBinding = binding;
      if (nonNull(currentBinding) && !isDestroyed.get()) {
        currentBinding.hint.setText("");
        currentBinding.hint.setBackgroundColor(Colors.TRANSPARENT);
        currentBinding.hint.setTextColor(Colors.WHITE);
        currentBinding.hint.setVisibility(View.INVISIBLE);
      }
    });
  }

  /**
   * Clear face rectangles with specific session type
   *
   * @param sessionType The type of session (determines log message)
   */
  public void clearFaceRectangles(SessionType sessionType) {
    if (isDestroyed.get()) {
      return;
    }

    // Clear face rectangles at start of new session
    synchronized (faceRects) {
      faceRects.clear();
      faceLandmarks.clear();
      personRects.clear();
    }

    // Clear overlay immediately for clean start
    runOnUiThread(() -> {
      if (binding != null) {
        binding.videoOverlay.setBackground(null);
      }
    });

    Timber.d(sessionType.getLogMessage());
  }

  /**
   * Handle successful authentication
   */
  public void onAuthSuccess(@NonNull String user,
                            @NonNull List<Rect> detectedFaceRects,
                            @NonNull List<List<Point>> faceLandmarks,
                            DeviceConfig.CameraRotation rotation,
                            float scaleFactor,
                            short score) {
    if (isDestroyed.get()) return;

    Timber.d("Authentication successful for user: %s", user);

    // Update feature structures
    synchronized (faceRects) {
      faceRects.clear();
      faceRects.addAll(detectedFaceRects);
    }
    synchronized (this.faceLandmarks) {
      this.faceLandmarks.clear();
      this.faceLandmarks.addAll(faceLandmarks);
    }
    synchronized (this.personRects) {
      this.personRects.clear();
      this.personRects.addAll(personRects);
    }

    String accesories = ""; // ToDo - find accessories in hints and create a friendly string
    String msg = String.format("%s(%d)%s", user, score, accesories);
    // Show success feedback with white text for good contrast on green background
    showFeedback(msg, Colors.SUCCESS_GREEN, Colors.WHITE, true);
    showCombinedOverlay(detectedFaceRects, faceLandmarks, personRects, scaleFactor, rotation);
  }

  /**
   * Handle authentication failure
   */
  public void onAuthFailure(@NonNull String status,
                            @NonNull List<Rect> detectedFaceRects,
                            DeviceConfig.CameraRotation rotation,
                            float scaleFactor) {
    if (isDestroyed.get()) {
      return;
    }

    Timber.e("Authentication failed: %s", status);

    // Update face rectangles with detected faces (if any)
    synchronized (faceRects) {
      faceRects.clear();
      faceRects.addAll(detectedFaceRects);
      faceLandmarks.clear();
      personRects.clear();
    }

    // White text provides good contrast on red background
    showFeedback(status, Colors.ERROR_RED, Colors.WHITE, true);
    showCombinedOverlay(detectedFaceRects, new ArrayList<>(), new ArrayList<>(), scaleFactor, rotation);
  }

  /**
   * Handle successful enrollment
   */
  public void onEnrollmentSuccess(@NonNull String name) {
    if (isDestroyed.get()) return;

    Timber.d("Enrollment successful for: %s", name);
    // White text provides good contrast on blue background
    showFeedback(name, Colors.INFO_BLUE, Colors.WHITE, true);
  }

  /**
   * Handle enrollment failure
   */
  public void onEnrollmentFailure(@NonNull String status) {
    if (isDestroyed.get()) return;

    Timber.d("Enrollment failed: %s", status);
    // White text provides good contrast on red background
    showFeedback(status, Colors.ERROR_RED, Colors.WHITE, true);
  }

  /**
   * Show a general hint message
   */
  public void showHint(@NonNull String hint) {
    if (isDestroyed.get()) return;

    Timber.d("Showing hint: %s", hint);
    // White text provides good contrast on blueish background
    showFeedback(hint, Colors.HINT_BLUEISH, Colors.WHITE, false);
  }

  /**
   * Show compatibility success message
   */
  public void onCompatibilitySuccess(@NonNull String message) {
    if (isDestroyed.get()) return;

    Timber.d("Compatibility check successful: %s", message);
    // White text provides good contrast on blue background
    showFeedback(message, Colors.INFO_BLUE, Colors.WHITE, true);
  }

  /**
   * Show compatibility failure message
   */
  public void onCompatibilityFailure(@NonNull String message) {
    if (isDestroyed.get()) return;

    Timber.d("Compatibility check failed: %s", message);
    // White text provides good contrast on red background
    showFeedback(message, Colors.ERROR_RED, Colors.WHITE, true);
  }

  /**
   * Show compatibility error message
   */
  public void onCompatibilityError(@NonNull String message) {
    if (isDestroyed.get()) return;

    Timber.d("Compatibility check error: %s", message);
    // White text provides good contrast on red background
    showFeedback(message, Colors.ERROR_RED, Colors.WHITE, true);
  }

  /**
   * Hide the hint immediately
   */
  public void hideHint() {
    if (isDestroyed.get()) return;

    cancelResetTaskAsync();
    runOnUiThread(() -> {
      FragmentPreviewBinding currentBinding = binding;
      if (nonNull(currentBinding) && !isDestroyed.get()) {
        currentBinding.hint.setVisibility(View.INVISIBLE);
      }
    });
  }

  /**
   * Core method to show feedback with specified colors and auto-reset option
   */
  private void showFeedback(@NonNull String message, int backgroundColor, int foregroundColor, boolean autoReset) {
    if (isDestroyed.get()) return;

    runOnUiThread(() -> {
      FragmentPreviewBinding currentBinding = binding;
      if (nonNull(currentBinding) && !isDestroyed.get()) {
        currentBinding.hint.setAlpha(0f);
        currentBinding.hint.setVisibility(View.VISIBLE);
        currentBinding.hint.setBackgroundColor(backgroundColor);
        currentBinding.hint.setTextColor(foregroundColor);
        currentBinding.hint.setText(message);

        // Animate the hint appearance with a fade-in effect
        currentBinding.hint.animate()
          .alpha(1.0f)
          .setDuration(300)
          .setStartDelay(100)
          .start();
      }
    });


    if (autoReset && !isDestroyed.get()) {
      resetHintDelayed();
    }
  }

  private void showCombinedOverlay(List<Rect> faceRects,
                                   List<List<Point>> landmarks,
                                   List<PersonRect> personRects,
                                   float scaleFactor,
                                   DeviceConfig.CameraRotation rotation) {
    if (isDestroyed.get()) return;

    View videoOverlay = binding != null ? binding.videoOverlay : null;
    if (videoOverlay == null) {
      return;
    }

    synchronized (this) {
      boolean hasContent = !faceRects.isEmpty() || !landmarks.isEmpty() || !personRects.isEmpty();

      if (!hasContent) {
        runOnUiThread(() -> videoOverlay.setBackground(null));
        return;
      }

      // Create a composite drawable that draws all elements
      Drawable combinedDrawable = new Drawable() {
        @Override
        public void draw(@NonNull Canvas canvas) {
          canvas.save();
          canvas.scale(-1f, 1f, getBounds().centerX(), getBounds().centerY());

          int canvasWidth = getBounds().width();
          int canvasHeight = getBounds().height();

          // Draw face rectangles (white)
          drawRects(canvas, faceRects, scaleFactor, rotation, canvasHeight, Colors.WHITE, 4f);

          // Draw person rectangles (blueish)
          List<Rect> convertedPersonRects = new ArrayList<>();
          for (PersonRect personRect : personRects) {
            Rect rect = new Rect(
              (int)personRect.getX(),
              (int)personRect.getY(),
              (int)(personRect.getX() + personRect.getW()),
              (int)(personRect.getY() + personRect.getH())
            );
            convertedPersonRects.add(rect);
          }
          drawRects(canvas, convertedPersonRects, scaleFactor, rotation, canvasHeight, Colors.HINT_BLUEISH, 4f);

          // Draw landmarks (white circles)
          drawLandmarks(canvas, landmarks, scaleFactor, rotation, canvasHeight, Colors.WHITE);

          canvas.restore();
        }

        private void drawRects(Canvas canvas, List<Rect> rects, float scale,
                               DeviceConfig.CameraRotation rot, int canvasH, int color, float strokeWidth) {
          if (rects.isEmpty()) return;

          Paint paint = new Paint();
          paint.setColor(color);
          paint.setStyle(Paint.Style.STROKE);
          paint.setStrokeWidth(strokeWidth);
          paint.setAntiAlias(true);

          for (Rect rect : rects) {
            float scaledLeft = rect.left * scale;
            float scaledTop = rect.top * scale;
            float scaledRight = rect.right * scale;
            float scaledBottom = rect.bottom * scale;

            float projectedLeft, projectedTop, projectedRight, projectedBottom;

            if (rot.equals(Rotation_90_Deg)) {
              projectedLeft = scaledTop;
              projectedTop = canvasH - scaledRight;
              projectedRight = scaledBottom;
              projectedBottom = canvasH - scaledLeft;
            }
            else if (rot.equals(Rotation_180_Deg)) {
              projectedLeft = scaledRight;
              projectedTop = scaledBottom;
              projectedRight = scaledLeft;
              projectedBottom = scaledTop;
            }
            else if (rot.equals(Rotation_270_Deg)) {
              projectedLeft = canvasH - scaledBottom;
              projectedTop = scaledLeft;
              projectedRight = canvasH - scaledTop;
              projectedBottom = scaledRight;
            }
            else {
              projectedLeft = scaledLeft;
              projectedTop = scaledTop;
              projectedRight = scaledRight;
              projectedBottom = scaledBottom;
            }

            canvas.drawRect(projectedLeft, projectedTop, projectedRight, projectedBottom, paint);
          }
        }

        private void drawLandmarks(Canvas canvas, List<List<Point>> landmarksList, float scale,
                                   DeviceConfig.CameraRotation rot, int canvasH, int color) {
          if (landmarksList.isEmpty()) return;

          Paint paint = new Paint();
          paint.setColor(color);
          paint.setStyle(Paint.Style.FILL);
          paint.setAntiAlias(true);

          for (List<Point> faceLandmarks : landmarksList) {
            for (Point landmark : faceLandmarks) {
              float scaledX = landmark.x * scale;
              float scaledY = landmark.y * scale;

              float projectedX, projectedY;

              if (rot.equals(Rotation_90_Deg)) {
                projectedX = scaledY;
                projectedY = canvasH - scaledX;
              }
              else if (rot.equals(Rotation_180_Deg)) {
                projectedX = scaledX;
                projectedY = scaledY;
              }
              else if (rot.equals(Rotation_270_Deg)) {
                projectedX = canvasH - scaledY;
                projectedY = scaledX;
              }
              else {
                projectedX = scaledX;
                projectedY = scaledY;
              }

              canvas.drawCircle(projectedX, projectedY, 5f, paint);
            }
          }
        }

        @Override
        public void setAlpha(int alpha) { }

        @Override
        public void setColorFilter(@Nullable ColorFilter colorFilter) { }

        @Override
        public int getOpacity() {
          return PixelFormat.TRANSLUCENT;
        }
      };

      runOnUiThread(() -> videoOverlay.setBackground(combinedDrawable));
    }
  }

  /**
   * Schedule automatic hint reset after delay
   */
  private void resetHintDelayed() {
    if (isDestroyed.get()) return;

    // Cancel any existing scheduled task
    cancelResetTaskAsync();

    try {
      // Schedule new reset task
      resetHintTask = scheduledExecutor.schedule(() -> {
        if (!isDestroyed.get()) {
          synchronized (faceRects) {
            faceRects.clear();
          }
          runOnUiThread(() -> {
            FragmentPreviewBinding currentBinding = binding;
            if (nonNull(currentBinding) && !isDestroyed.get()) {
              // Store the original position
              float originalY = currentBinding.hint.getY();

              currentBinding.hint.animate()
                .translationY(-currentBinding.hint.getHeight()) // Slide upwards by the height of the view
                .setDuration(300)
                .withEndAction(() -> {
                  currentBinding.hint.setVisibility(View.INVISIBLE);
                  // Restore original position for future animations
                  currentBinding.hint.setTranslationY(0f);
                })
                .start();

              currentBinding.videoOverlay.setBackground(null);
            }
          });
        }
      }, HINT_RESET_DELAY_SECONDS, TimeUnit.SECONDS);
    }
    catch (Exception e) {
      Timber.w(e, "Failed to schedule hint reset");
    }
  }

  /**
   * Cancel any pending reset task asynchronously to prevent ANR
   */
  private void cancelResetTaskAsync() {
    ScheduledFuture<?> currentTask = resetHintTask;
    if (currentTask != null && !currentTask.isDone()) {
      // Cancel asynchronously to prevent blocking
      try {
        currentTask.cancel(false); // Don't interrupt if running
      }
      catch (Exception e) {
        Timber.w(e, "Error canceling reset task");
      }
    }
  }

  /**
   * Execute code on UI thread with error handling
   */
  private void runOnUiThread(@NonNull Runnable action) {
    if (isDestroyed.get()) return;

    try {
      uiThreadRunner.accept(action);
    }
    catch (Exception e) {
      Timber.w(e, "Error running UI thread action");
    }
  }

  /**
   * Cleanup resources - designed to be ANR-free
   */
  public void cleanup() {
    Timber.d("Cleaning up UIFeedbackHelper");

    // Mark as destroyed first to prevent new operations
    isDestroyed.set(true);

    // Cancel scheduled task asynchronously
    cancelResetTaskAsync();

    // Clear references immediately
    binding = null;
    resetHintTask = null;

    // Clear face rects safely
    synchronized (faceRects) {
      faceRects.clear();
    }
  }

  /**
   * Draw bounding boxes on bitmap
   */
  public void drawBoundingBox(Bitmap bitmap, float ratio) {
    if (isDestroyed.get() || bitmap == null || bitmap.isRecycled()) {
      Timber.w("Cannot draw bounding box: bitmap is null, recycled, or helper is destroyed");
      return;
    }

    try {
      Paint paint = new Paint();
      paint.setColor(Color.GREEN);
      paint.setAlpha(200);
      paint.setStyle(Paint.Style.STROKE);

      Canvas canvas = new Canvas(bitmap);

      synchronized (faceRects) {
        for (Rect face : faceRects) {
          if (face != null) {
            Rect adjusted = new Rect(face);
            adjusted.top = (int)(face.top * ratio);
            adjusted.bottom = (int)(face.bottom * ratio);
            adjusted.left = (int)(face.left * ratio);
            adjusted.right = (int)(face.right * ratio);
            canvas.drawRect(adjusted, paint);
          }
        }
      }
    }
    catch (Exception e) {
      Timber.w(e, "Error drawing bounding box");
    }
  }

  /**
   * Get the current hint visibility state
   */
  public boolean isHintVisible() {
    if (isDestroyed.get()) return false;

    FragmentPreviewBinding currentBinding = binding;
    return nonNull(currentBinding) && currentBinding.hint.getVisibility() == View.VISIBLE;
  }

  /**
   * Get the current hint text
   */
  @Nullable
  public String getCurrentHintText() {
    if (isDestroyed.get()) return null;

    FragmentPreviewBinding currentBinding = binding;
    if (nonNull(currentBinding)) {
      try {
        return currentBinding.hint.getText().toString();
      }
      catch (Exception e) {
        Timber.w(e, "Error getting hint text");
      }
    }
    return null;
  }

  /**
   * Check if there are any face rectangles detected
   */
  public boolean hasFaceRects() {
    if (isDestroyed.get()) return false;

    synchronized (faceRects) {
      return !faceRects.isEmpty();
    }
  }

  /**
   * Get the count of detected face rectangles
   */
  public int getFaceRectCount() {
    if (isDestroyed.get()) return 0;

    synchronized (faceRects) {
      return faceRects.size();
    }
  }

  /**
   * Enum to specify the type of session when clearing face rectangles
   */
  public enum SessionType {
    AUTHENTICATION("Authentication session started - face rectangles cleared"),
    ENROLLMENT("Enrollment session started - face rectangles cleared"),
    GENERAL("Face rectangles cleared and overlay reset");

    private final String logMessage;

    SessionType(String logMessage) {
      this.logMessage = logMessage;
    }

    public String getLogMessage() {
      return logMessage;
    }
  }

  // Feedback colors
  private static final class Colors {
    static final int TRANSPARENT = Color.argb(255, 0, 0, 0);
    static final int SUCCESS_GREEN = Color.argb(200, 10, 200, 10);
    static final int ERROR_RED = Color.argb(200, 200, 10, 10);
    static final int INFO_BLUE = Color.argb(200, 10, 10, 200);
    static final int HINT_BLUEISH = Color.argb(200, 50, 50, 255);

    // Foreground colors for contrast
    static final int WHITE = Color.WHITE;
    static final int BLACK = Color.BLACK;
  }
}

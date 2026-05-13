// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util.enroll;

import static java.util.Objects.isNull;
import static java.util.Objects.nonNull;
import static java.util.Objects.requireNonNull;

import android.content.Context;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.realsenseai.rsid.api.EnrollFaceprintsExtractionCallback;
import com.realsenseai.rsid.api.EnrollStatus;
import com.realsenseai.rsid.api.ExtractedFaceprints;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.api.FacePose;
import com.realsenseai.rsid.api.FaceRectVector;
import com.realsenseai.rsid.sample.callbacks.EnrollmentCallback;
import com.realsenseai.rsid.sample.db.AppDatabase;
import com.realsenseai.rsid.sample.db.User;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicBoolean;
import timber.log.Timber;

public class HostEnrollmentHelper extends EnrollFaceprintsExtractionCallback {
  private static final String TAG = "HostEnrollmentHelper";
  private static final int FEATURE_VECTOR_SIZE = 515;

  // Shared executor for database operations to avoid creating multiple threads
  private static final ExecutorService DATABASE_EXECUTOR = Executors.newSingleThreadExecutor(
    r -> new Thread(r, "HostEnrollment-DB"));

  private final Context context;
  private final AtomicBoolean isDestroyed = new AtomicBoolean(false);

  // Cached database instance to avoid repeated getInstance calls
  private final AppDatabase database;

  // Volatile fields for thread safety
  private volatile FaceAuthenticator authenticator;
  private volatile String name;
  private volatile EnrollmentCallback callback;
  private volatile Future<?> imageEnrollmentTask;
  private volatile boolean resultProcessed = false;

  public HostEnrollmentHelper(@NonNull Context context, FaceAuthenticator authenticator) {
    this.context = requireNonNull(context, "Context cannot be null");
    this.authenticator = authenticator;
    this.database = AppDatabase.getInstance(context);
  }

  @Override
  public void OnHint(@NonNull EnrollStatus hint, float frameScore) {
    if (isDestroyed.get()) {
      return;
    }
    Timber.tag(TAG).i("Enroll hint: %s, score = %f", hint, frameScore);
  }

  @Override
  public void OnFaceDetected(@NonNull FaceRectVector faceRects,
                             long faceCount) {
    if (isDestroyed.get()) {
      return;
    }
    Timber.tag(TAG).i("Face detected, count: %d", faceCount);
  }

  @Override
  public void OnProgress(@NonNull FacePose pose) {
    if (isDestroyed.get()) {
      return;
    }
    Timber.tag(TAG).i("Enrollment progress: %s", pose);
  }

  @Override
  public void OnResult(@NonNull EnrollStatus status,
                       @Nullable ExtractedFaceprints faceprints) {

    // Prevent processing multiple results - ignore subsequent Error results
    // after a valid result
    if (resultProcessed) {
      Timber.d("HostEnrollmentHelper: Ignoring duplicate result: %s",
               status.toString());
      return;
    }

    // Mark result as processed to prevent duplicate processing
    resultProcessed = true;

    if (isNull(authenticator)) {
      if (nonNull(callback)) {
        requireNonNull(callback).onEnrollmentFailure("Error: Reattach camera");
      }
      return;
    }
    else {
      // This ensures clean enrollment
      authenticator.Cancel();
      authenticator.Disconnect();
      authenticator.Connect(SDKWrapper.INSTANCE.getCachedOrNewSerialConfig());
    }

    if (isDestroyed.get()) {
      return;
    }

    Timber.tag(TAG).i("Enrollment result: %s", status);

    if (status == EnrollStatus.Success && nonNull(faceprints)) {
      commitFacePrintsToDB(requireNonNull(faceprints));
    }
    else {
      handleEnrollmentFailure(status.toString());
    }
  }

  private void commitFacePrintsToDB(@NonNull ExtractedFaceprints faceprints) {
    if (isDestroyed.get()) {
      return;
    }

    try {
      var element = faceprints.getData();
      if (isNull(element)) {
        handleEnrollmentFailure("Invalid faceprints data");
        return;
      }

      var featureVector = element.getFeaturesVector();
      if (isNull(featureVector)) {
        handleEnrollmentFailure("Invalid feature vector");
        return;
      }

      // Pre-allocate and initialize feature vector with mask
      var featureVectorWithMask = new short[FEATURE_VECTOR_SIZE];
      Arrays.fill(featureVectorWithMask, (short)0);

      var user = new User.Builder()
        .setName(name)
        .setEnrollmentDescriptor(featureVector)
        .setAdaptiveDescriptorWithMask(featureVectorWithMask)
        .setAdaptiveDescriptorWithoutMask(featureVector)
        .setFeaturesType(element.getFeaturesType())
        .setFlags(element.getFlags())
        .setVersion(element.getVersion())
        .build();

      Timber.tag(TAG).d("Inserting user: %s", user.toString());

      // Use shared executor for database operations
      DATABASE_EXECUTOR.execute(() -> insertUserToDB(user));
    }
    catch (Exception e) {
      Timber.tag(TAG).e(e, "Error preparing faceprints for database");
      handleEnrollmentFailure("Error processing faceprints");
    }
  }

  private void insertUserToDB(@NonNull User user) {
    if (isDestroyed.get()) {
      return;
    }

    try {
      database.userDao().insertUser(user);
      handleEnrollmentSuccess(user.name);
      Timber.tag(TAG).d("User successfully inserted: %s", user.name);
    }
    catch (Exception e) {
      Timber.tag(TAG).e(e, "Database insertion failed for user: %s", user.name);
      handleEnrollmentFailure("Database error: " + e.getMessage());
    }
  }

  private void handleEnrollmentSuccess(@NonNull String userName) {
    var currentCallback = callback;
    if (nonNull(currentCallback)) {
      currentCallback.onEnrollmentSuccess(userName);
    }
    callback = null;
  }

  private void handleEnrollmentFailure(@NonNull String error) {
    var currentCallback = callback;
    if (nonNull(currentCallback)) {
      currentCallback.onEnrollmentFailure(error);
    }
    callback = null;
  }

  public void Enroll(@NonNull String name,
                     @Nullable EnrollmentCallback callback) {
    if (isDestroyed.get()) {
      if (nonNull(callback)) {
        callback.onEnrollmentFailure("Helper is destroyed");
      }
      return;
    }

    this.name = requireNonNull(name, "Name cannot be null");
    this.callback = callback;
    resultProcessed = false; // Reset the flag for new enrollment attempt

    authenticator.Cancel();
    authenticator.Disconnect();
    authenticator.Connect(SDKWrapper.INSTANCE.getCachedOrNewSerialConfig());

    try {
      authenticator.ExtractFaceprintsForEnroll(this);
    }
    catch (Exception e) {
      Timber.tag(TAG).e(e, "Failed to start enrollment");
      handleEnrollmentFailure("Failed to start enrollment: " + e.getMessage());
    }
  }

  public void EnrollImage(@NonNull String name, @NonNull byte[] buffer,
                          long width, long height,
                          @Nullable EnrollmentCallback callback) {
    if (isDestroyed.get()) {
      if (nonNull(callback)) {
        callback.onEnrollmentFailure("Helper is destroyed");
      }
      return;
    }

    this.name = requireNonNull(name, "Name cannot be null");
    this.callback = callback;

    authenticator.Cancel();
    authenticator.Disconnect();
    authenticator.Connect(SDKWrapper.INSTANCE.getCachedOrNewSerialConfig());

    // Cancel any existing image enrollment task
    cancelImageEnrollmentTask();

    imageEnrollmentTask = DATABASE_EXECUTOR.submit(
      ()
        -> processImageEnrollment(
        requireNonNull(buffer, "Buffer cannot be null"), width,
        height));
  }

  private void processImageEnrollment(@NonNull byte[] buffer, long width,
                                      long height) {
    if (isDestroyed.get()) {
      return;
    }

    try {
      var faceprints = new ExtractedFaceprints();
      var currentAuthenticator = authenticator;

      if (isNull(currentAuthenticator)) {
        handleEnrollmentFailure("Authenticator is null");
        return;
      }

      var status = currentAuthenticator.EnrollImageFeatureExtraction(
        name, buffer, width, height, faceprints);

      if (status == EnrollStatus.Success) {
        Timber.tag(TAG).i("Image enrollment success for %s", name);
        commitFacePrintsToDB(faceprints);
      }
      else {
        Timber.tag(TAG).w("Image enrollment failed for %s. Status: %s", name,
                          status);
        handleEnrollmentFailure(status.toString());
      }
    }
    catch (Exception e) {
      Timber.tag(TAG).e(e, "Image enrollment exception for %s", name);
      handleEnrollmentFailure("Enrollment failed: " + e.getMessage());
    }
    finally {
      authenticator.Disconnect();
      SDKWrapper.INSTANCE.closeConnection();
    }
  }

  private void cancelImageEnrollmentTask() {
    var currentTask = imageEnrollmentTask;
    if (nonNull(currentTask) && !currentTask.isDone()) {
      currentTask.cancel(true);
    }
  }

  /**
   * Clean up resources. Should be called when the helper is no longer needed.
   */
  public void destroy() {
    if (isDestroyed.getAndSet(true)) {
      return; // Already destroyed
    }

    Timber.tag(TAG).d("Destroying HostEnrollmentHelper");

    // Cancel any ongoing tasks
    cancelImageEnrollmentTask();

    // Clear references
    callback = null;
    authenticator = null;
    name = null;
  }

  /**
   * Check if the helper has been destroyed
   */
  public boolean isDestroyed() { return isDestroyed.get(); }
}
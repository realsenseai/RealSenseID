// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util.enroll;

import static androidx.core.util.ObjectsCompat.requireNonNull;
import static java.util.Objects.isNull;
import static java.util.Objects.nonNull;

import android.content.Context;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.realsenseai.rsid.api.EnrollStatus;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.api.FacePose;
import com.realsenseai.rsid.api.FaceRectVector;
import com.realsenseai.rsid.api.Status;
import com.realsenseai.rsid.sample.callbacks.EnrollmentCallback;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import timber.log.Timber;

public class DeviceEnrollmentHelper extends com.realsenseai.rsid.api.EnrollmentCallback {
  // TODO: Implement full device mode

  private final Context context;
  private String name;
  private EnrollmentCallback callback;
  private Thread imageEnrollmentThread;
  private boolean resultProcessed = false;

  private volatile FaceAuthenticator authenticator;

  public DeviceEnrollmentHelper(Context context, FaceAuthenticator authenticator) {
    this.context = context;
    this.authenticator = authenticator;
  }

  public void Enroll(@NonNull String name, @Nullable EnrollmentCallback callback) {
    this.name = name;
    this.callback = callback;
    resultProcessed = false; // Reset the flag for new enrollment attempt

    if (isNull(authenticator)) {
      if (nonNull(callback)) {
        requireNonNull(callback).onEnrollmentFailure("Error: Reattach camera");
      }
      return;
    }
    else {
      // This ensures clean auth
      requireNonNull(authenticator).Disconnect();
      requireNonNull(authenticator).Connect(SDKWrapper.INSTANCE.getCachedOrNewSerialConfig());
    }

    var status = requireNonNull(authenticator).Enroll(this, this.name);
    if (status != Status.Ok) {
      // Only call failure callback if we haven't already processed a result
      if (!resultProcessed) {
        requireNonNull(callback).onEnrollmentFailure(status.toString());
      }
      else {
        Timber.d("DeviceEnrollmentHelper: Skipping failure callback - result already processed");
      }
    }
    else {
      requireNonNull(callback).onEnrollmentSuccess(name + " enrolled");
    }
  }

  @Override
  protected void OnResult(EnrollStatus status) {
    Timber.d("Enroll: OnResult: EnrollStatus = %s", status.toString());

    // Prevent processing multiple results - ignore subsequent Error results
    // after a valid result
    if (resultProcessed) {
      Timber.d("DeviceEnrollmentHelper: Ignoring duplicate result: %s", status.toString());
      return;
    }

    // Mark result as processed to prevent duplicate processing
    resultProcessed = true;

    // authenticator.Disconnect();   // TODO: Fix crash on disconnect
    SDKWrapper.INSTANCE.closeConnection();

    try {
      if (nonNull(callback)) {
        if (status == EnrollStatus.Success) {
          Timber.i("Enroll success for %s", name);
          requireNonNull(callback).onEnrollmentSuccess(name);
        }
        else {
          Timber.e("Enroll failure for %s. Status: %s", name, status.toString());
          requireNonNull(callback).onEnrollmentFailure(status.toString());
        }
      }
    }
    catch (Exception e) {
      Timber.e(e);
      if (nonNull(callback)) {
        requireNonNull(callback).onEnrollmentFailure(e.getMessage());
      }
    }
    finally {
      callback = null;
    }
  }

  @Override
  protected void OnProgress(FacePose facePose) {
    Timber.d("Enroll: OnProgress");
  }

  @Override
  protected void OnHint(EnrollStatus enrollStatus, float frameScore) {
    Timber.d("Enroll: OnHint: EnrollStatus = %s, frameScore = %f", enrollStatus.toString(), frameScore);
    if (nonNull(callback)) {
      callback.onEnrollmentHint(enrollStatus.toString());
    }
  }

  @Override
  protected void OnFaceDetected(FaceRectVector faceRectVector, long l) {
    Timber.d("Enroll: OnFaceDetected: faceRectVector = %s", faceRectVector.toString());
  }
}

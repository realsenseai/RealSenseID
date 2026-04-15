// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util.auth;

import static java.util.Objects.isNull;
import static java.util.Objects.nonNull;
import static java.util.Objects.requireNonNull;

import android.content.Context;
import android.graphics.Point;
import android.graphics.Rect;
import androidx.annotation.Nullable;
import com.realsenseai.rsid.api.AuthenticateStatus;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.api.FaceLandmarks;
import com.realsenseai.rsid.api.FaceLandmarksVector;
import com.realsenseai.rsid.api.FaceRectVector;
import com.realsenseai.rsid.api.PersonRect;
import com.realsenseai.rsid.api.PersonRectVector;
import com.realsenseai.rsid.api.Status;
import com.realsenseai.rsid.sample.callbacks.AuthenticationCallback;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import java.util.ArrayList;
import java.util.List;
import timber.log.Timber;

public class DeviceAuthenticationHelper extends com.realsenseai.rsid.api.AuthenticationCallback {
  protected final List<Rect> faceRects = new ArrayList<>();

  // This structure has more info that just a rectangle on the C++ side
  // We'll use the native PersonRect structure to hold the data
  // but we will also manage its memory on the Java side.
  // For customers not interested in the extra data, they can just use the Rect info.
  // and switch to Rect from Android graphics if needed.
  protected final List<PersonRect> personRects = new ArrayList<>();
  protected final List<List<Point>> faceLandmarks = new ArrayList<>();

  protected final Context context;
  protected FaceAuthenticator authenticator;
  protected AuthenticationCallback callback;
  protected boolean resultProcessed = false;

  public DeviceAuthenticationHelper(Context context, FaceAuthenticator authenticator) {
    this.context = context;
    this.authenticator = authenticator;
  }

  @Override
  public void OnResult(AuthenticateStatus authenticateStatus, String name, short score) {
    Timber.d("DeviceAuthenticatorHelper: OnResult: Status: %s, Name: %s, Score: %d", authenticateStatus.toString(), name, score);

    // Prevent processing multiple results - ignore subsequent Error results
    // after a valid result
    if (resultProcessed) {
      Timber.d("DeviceAuthenticatorHelper: Ignoring duplicate result: %s", authenticateStatus.toString());
      return;
    }

    // Mark result as processed to prevent duplicate processing
    resultProcessed = true;

    if (authenticateStatus == AuthenticateStatus.Success) {
      String result = "";
      if (name.isEmpty()) {
        result = authenticateStatus.toString();
      }
      else {
        result = name; // This is needed so that the new var is allocated by Java instead of cpp
      }
      if (nonNull(callback)) {
        callback.onAuthSuccess(result, faceRects, faceLandmarks, score);
      }
    }
    else {
      if (nonNull(callback)) {
        callback.onAuthFailure(authenticateStatus.toString(), faceRects);
      }
    }
    callback = null;
  }

  @Override
  public void OnPersonDetected(PersonRectVector persons, long ts) {
    Timber.d("DeviceAuthenticatorHelper: OnPersonDetected");
    // Deep copy person rects in order to be the pointer owner
    for (var personRect : persons) {
      var p = new PersonRect();
      p.setX(personRect.getX());
      p.setY(personRect.getY());
      p.setW(personRect.getW());
      p.setH(personRect.getH());
      p.setId(personRect.getId());
      p.setBody_part(personRect.getBody_part());
      personRects.add(p);
    }
  }

  @Override
  public void OnLandmarksDetected(FaceLandmarksVector landmarks, long ts) {
    Timber.d("DeviceAuthenticatorHelper: OnLandmarksDetected");
    for (FaceLandmarks landmark : landmarks) {
      // C++ side:
      // #define NUM_FACE_LANDMARKS 5
      /**
       * Detected face landamrks
       * Left eye, right eye, nose tip, mouth left, mouth right
       */

      // Let's rely on the size of the array here in order to avoid issues
      // with NUM_FACE_LANDMARKS changes in the future.
      List<Point> faceLandmarkPoints = new ArrayList<>();
      for (int i = 0; i < landmark.getLm_x().length; i++) {
        int x = (int)landmark.getLm_x()[i];
        int y = (int)landmark.getLm_y()[i];
        Timber.d("Landmark %d: x=%d, y=%d", i, x, y);
        faceLandmarkPoints.add(new Point(x, y));
      }
      Timber.d("Landmark total points: %d", faceLandmarkPoints.size());
      faceLandmarks.add(faceLandmarkPoints);
    }
    Timber.d("Landmark grand total faces: %d", faceLandmarks.size());
  }

  @Override
  public void OnFaceDetected(FaceRectVector faceRectVector, long faceCount) {
    Timber.d("DeviceAuthenticatorHelper: OnFaceDetected");
    try {
      if (faceRectVector == null) {
        Timber.w("FaceRectVector is null");
        return;
      }

      long vectorSize = faceRectVector.size();
      long actualCount = Math.min(faceCount, vectorSize);

      Timber.d("Face count: %d, Vector size: %d, Using: %d", faceCount, vectorSize, actualCount);

      for (int i = 0; i < actualCount; i++) {
        var vec = faceRectVector.get(i);
        if (vec != null) {
          var rect = new Rect((int)vec.getX(), (int)vec.getY(), (int)(vec.getX() + vec.getW()), (int)(vec.getY() + vec.getH()));
          faceRects.add(rect);
        }
      }
    }
    catch (Exception e) {
      Timber.e(e);
    }
  }

  @Override
  public void OnHint(AuthenticateStatus hint, float frameScore) {
    Timber.d("DeviceAuthenticatorHelper: OnHint: %s, frameScore: %f", hint, frameScore);
    if (nonNull(callback)) {
      callback.onAuthHint(hint.toString(), frameScore);
    }
  }

  public void setAuthenticator(FaceAuthenticator authenticator) {
    this.authenticator = authenticator;
  }

  public void Authenticate(@Nullable AuthenticationCallback callback) {
    this.callback = callback;
    resultProcessed = false; // Reset the flag for new authentication attempt
    faceRects.clear();
    personRects.clear();
    faceLandmarks.clear();

    if (isNull(authenticator)) {
      if (nonNull(callback)) {
        requireNonNull(callback).onAuthFailure("Error: Reattach camera", faceRects);
      }
      return;
    }
    else {
      // This ensures clean auth
      authenticator.Disconnect();
      authenticator.Connect(SDKWrapper.INSTANCE.getCachedOrNewSerialConfig());
    }

    var status = requireNonNull(authenticator).Authenticate(this);
    if (status == Status.Ok) {
      Timber.d("DeviceAuthenticatorHelper: Authenticate: Success");
    }
    else {
      Timber.d("DeviceAuthenticatorHelper: Authenticate: Failure");
      // Only call failure callback if we haven't already processed a result
      if (nonNull(callback) && !resultProcessed) {
        requireNonNull(callback).onAuthFailure(status.toString(), faceRects);
      }
      else if (resultProcessed) {
        Timber.d("DeviceAuthenticatorHelper: Skipping failure callback - result already processed");
      }
    }
  }
}

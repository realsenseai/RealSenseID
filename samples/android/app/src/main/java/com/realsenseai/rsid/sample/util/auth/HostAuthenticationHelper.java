// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util.auth;

import static java.util.Objects.isNull;
import static java.util.Objects.nonNull;
import static java.util.Objects.requireNonNull;

import android.content.Context;
import android.graphics.Point;
import android.graphics.Rect;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.realsenseai.rsid.api.AuthFaceprintsExtractionCallback;
import com.realsenseai.rsid.api.AuthenticateStatus;
import com.realsenseai.rsid.api.DBFaceprintsElement;
import com.realsenseai.rsid.api.ExtractedFaceprints;
import com.realsenseai.rsid.api.ExtractedFaceprintsElement;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.api.FaceLandmarks;
import com.realsenseai.rsid.api.FaceLandmarksVector;
import com.realsenseai.rsid.api.FaceRectVector;
import com.realsenseai.rsid.api.Faceprints;
import com.realsenseai.rsid.api.MatchElement;
import com.realsenseai.rsid.api.MatchResultHost;
import com.realsenseai.rsid.api.PersonRect;
import com.realsenseai.rsid.api.ThresholdsConfidenceEnum;
import com.realsenseai.rsid.sample.callbacks.AuthenticationCallback;
import com.realsenseai.rsid.sample.db.AppDatabase;
import com.realsenseai.rsid.sample.db.User;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import timber.log.Timber;

public class HostAuthenticationHelper extends AuthFaceprintsExtractionCallback {

  protected final List<Rect> faceRects = new ArrayList<>();
  protected final List<PersonRect> personRects = new ArrayList<>();
  protected final List<List<Point>> faceLandmarks = new ArrayList<>();

  protected final Context context;
  protected FaceAuthenticator authenticator;
  protected AuthenticationCallback callback;
  protected boolean resultProcessed = false;

  public HostAuthenticationHelper(Context context, FaceAuthenticator authenticator) {
    this.context = context;
    this.authenticator = authenticator;
  }

  @NonNull
  private static DBFaceprintsElement getDbFaceprintsElement(User user) {
    Timber.d(user.toString());
    DBFaceprintsElement dbFaceprintsElement = new DBFaceprintsElement();
    dbFaceprintsElement.setFlags(user.getFlags());
    dbFaceprintsElement.setVersion(user.getVersion());
    dbFaceprintsElement.setFeaturesType(user.getFeaturesType());
    dbFaceprintsElement.setAdaptiveDescriptorWithoutMask(
      user.getAdaptiveDescriptorWithoutMask().clone());
    dbFaceprintsElement.setEnrollmentDescriptor(
      user.getEnrollmentDescriptor().clone());
    return dbFaceprintsElement;
  }

  public void OnFaceDetected(FaceRectVector faceRectVector, long faceCount) {
    Timber.d("Auth: OnFaceDetected");
    try {
      if (faceRectVector == null) {
        Timber.w("FaceRectVector is null");
        return;
      }

      long vectorSize = faceRectVector.size();
      long actualCount = Math.min(faceCount, vectorSize);

      Timber.d("Face count: %d, Vector size: %d, Using: %d", faceCount,
               vectorSize, actualCount);

      for (int i = 0; i < actualCount; i++) {
        var vec = faceRectVector.get(i);
        if (vec != null) {
          var rect = new Rect((int)vec.getX(), (int)vec.getY(),
                              (int)(vec.getX() + vec.getW()),
                              (int)(vec.getY() + vec.getH()));
          faceRects.add(rect);
        }
      }
    }
    catch (Exception e) {
      Timber.e(e);
    }
  }

  public void OnHint(AuthenticateStatus hint, float frameScore) {
    Timber.d("Auth: OnHint: %s, frameScore: %f", hint, frameScore);
  }

  @Override
  public void OnResult(AuthenticateStatus status,
                       ExtractedFaceprints faceprints) {
    Timber.d("Auth: OnResult");
    Timber.i("Auth complete! Status: %s", status);

    // Prevent processing multiple results - ignore subsequent Error results
    // after a valid result
    if (resultProcessed) {
      Timber.d("HostAuthenticationHelper: Ignoring duplicate result: %s",
               status.toString());
      return;
    }

    // Mark result as processed to prevent duplicate processing
    resultProcessed = true;

    if (status == AuthenticateStatus.Success) {

      var extractedFaceprintsElement = new ExtractedFaceprintsElement();
      extractedFaceprintsElement.setVersion(faceprints.getData().getVersion());
      extractedFaceprintsElement.setFlags(faceprints.getData().getFlags());
      extractedFaceprintsElement.setFeaturesType(
        faceprints.getData().getFeaturesType());
      var matchElement = new MatchElement();
      extractedFaceprintsElement.setFeaturesVector(
        faceprints.getData().getFeaturesVector().clone());
      matchElement.setData(extractedFaceprintsElement);
      var updated = new Faceprints();

      final AtomicInteger maxScore = new AtomicInteger(-1);
      new Thread(() -> {
        var dbUsers = AppDatabase.getInstance(context).userDao().getAll();
        MatchResultHost winnerResultHost = null;
        User winnerUser = null;
        Faceprints winner_updated_faceprints = null;

        for (User user : dbUsers) {
          var dbFaceprintsElement = getDbFaceprintsElement(user);
          var dbFaceprintsElementCopy = getDbFaceprintsElement(user);
          var dbFaceprints = new Faceprints();
          dbFaceprints.setData(dbFaceprintsElement);
          updated.setData(dbFaceprintsElementCopy); // init updated to previous
          // state in the DB.

          var matchResultHost = authenticator.MatchFaceprints(
            matchElement, dbFaceprints, updated,
            ThresholdsConfidenceEnum.ThresholdsConfidenceLevel_Medium);

          if (matchResultHost.getSuccess()) {
            if (matchResultHost.getScore() > maxScore.get()) {
              maxScore.set(matchResultHost.getScore());
              winnerResultHost = matchResultHost;
              winnerUser = user;
              winner_updated_faceprints = updated;
            }
          }
        }

        if (nonNull(winnerUser)) {
          if (requireNonNull(winnerResultHost).getShould_update()) {
            Timber.d("Updating DB");
            // Save: winner_updated_faceprints to DB
            DBFaceprintsElement element = winner_updated_faceprints.getData();
            winnerUser.setFlags(element.getFlags());
            winnerUser.setVersion(element.getVersion());
            winnerUser.setFeaturesType(element.getFeaturesType());
            winnerUser.setEnrollmentDescriptor(
              element.getEnrollmentDescriptor());
            winnerUser.setAdaptiveDescriptorWithoutMask(
              element.getAdaptiveDescriptorWithoutMask());
            AppDatabase.getInstance(context).userDao().updateUser(winnerUser);
          }
          Timber.i("Found user: %s", requireNonNull(winnerUser).name);

          if (nonNull(callback)) {
            callback.onAuthSuccess(winnerUser.getName(),
                                   faceRects,
                                   faceLandmarks,
                                   (short)maxScore.get());
          }
        }
        else {
          if (nonNull(callback)) {
            callback.onAuthFailure("Unrecognized User", faceRects);
          }
        }
        callback = null;
      }).start();
    }
    else { // status == AuthenticateStatus.Success
      if (nonNull(callback)) {
        callback.onAuthFailure(status.toString(), faceRects);
        callback = null;
      }
    }
  }

  @Override
  public void OnLandmarksDetected(FaceLandmarksVector landmarks, long ts) {
    Timber.d("HostAuthenticatorHelper: OnLandmarksDetected");
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

    requireNonNull(authenticator).ExtractFaceprintsForAuth(this);
    Timber.i("Authenticate started");
  }
}

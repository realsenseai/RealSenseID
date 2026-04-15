// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.callbacks;

import android.graphics.Point;
import android.graphics.Rect;
import androidx.annotation.NonNull;
import java.util.List;

public interface AuthenticationCallback {
  void onAuthSuccess(@NonNull String name,
                     @NonNull List<Rect> faceRects,
                     @NonNull List<List<Point>> faceLandmarks,
                     short score);

  void onAuthFailure(@NonNull String status, @NonNull List<Rect> faceRects);

  void onAuthHint(@NonNull String hint, float frameScore);
}

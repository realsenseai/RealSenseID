// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util;

import android.graphics.PointF;
import androidx.annotation.NonNull;
import java.util.List;
import org.opencv.core.Mat;
import org.opencv.core.Rect;
import org.opencv.core.Rect2d;
import org.opencv.core.Scalar;
import org.opencv.core.Size;
import org.opencv.imgproc.Imgproc;

public class FaceNormalizer {

  public static final int NORMALIZED_FACE_IMAGE_HEIGHT = 144;
  public static final int NORMALIZED_FACE_IMAGE_WIDTH = 144;
  private static final float NORMALIZED_FACE_IMAGE_EXPAND = 0.90625f;


  @NonNull
  public Mat getNormFace(Mat rgbImage, List<PointF> landmarks) {
    var newSize = new Size(NORMALIZED_FACE_IMAGE_WIDTH, NORMALIZED_FACE_IMAGE_HEIGHT);
    var normedImage = new Mat(newSize, rgbImage.type());

    var targetNormRect = getTargetNormRect(landmarks, NORMALIZED_FACE_IMAGE_EXPAND);
    var validRect = getValidRect(rgbImage.cols(), rgbImage.rows(), targetNormRect);
    var normedValidRect = getNormedValidRect(targetNormRect, validRect, normedImage.cols(), normedImage.rows());

    normedValidRect = getValidRect(normedImage.cols(), normedImage.rows(), normedValidRect);

    var orgFaceCrop = new Mat(rgbImage, validRect);
    if ((normedValidRect.width != normedImage.cols()) || (normedValidRect.height != normedImage.rows())) {
      normedImage.setTo(new Scalar(0, 0, 0));
    }
    var cropOutImg = new Mat(normedImage, normedValidRect);
    Imgproc.resize(orgFaceCrop, cropOutImg, cropOutImg.size(), 0, 0, Imgproc.INTER_AREA);
    return normedImage;
  }


  @NonNull
  private Rect getNormedValidRect(Rect rect, Rect valid_rect, int outWidth, int outHeight) {
    float scale = (float)outWidth / (float)rect.width;
    int x0 = rect.x;
    int y0 = rect.y;
    int x1 = rect.x + rect.width;
    int y1 = rect.y + rect.height;
    int x0_valid = valid_rect.x;
    int y0_valid = valid_rect.y;
    int x1_valid = valid_rect.x + valid_rect.width;
    int y1_valid = valid_rect.y + valid_rect.height;
    int margin_x0 = (int)((x0_valid - x0) * scale + 0.5f);
    int margin_y0 = (int)((y0_valid - y0) * scale + 0.5f);
    int margin_x1 = (int)((x1 - x1_valid) * scale + 0.5f);
    int margin_y1 = (int)((y1 - y1_valid) * scale + 0.5f);
    Rect normedValidRect = new Rect();
    normedValidRect.x = margin_x0;
    normedValidRect.y = margin_y0;
    normedValidRect.width = outWidth - margin_x0 - margin_x1;
    normedValidRect.height = outHeight - margin_y0 - margin_y1;
    return normedValidRect;
  }

  @NonNull
  private Rect getTargetNormRect(List<PointF> landmarks, float factor) {
    if (landmarks.size() != 5) {
      throw new RuntimeException("Face landmarks count must be 5");
    }

    double x_min = landmarks.get(0).x;
    double y_min = landmarks.get(0).y;
    double x_max = landmarks.get(0).x;
    double y_max = landmarks.get(0).y;

    for (PointF landmark : landmarks) {
      x_min = Math.min(x_min, landmark.x);
      y_min = Math.min(y_min, landmark.y);
      x_max = Math.max(x_max, landmark.x);
      y_max = Math.max(y_max, landmark.y);
    }

    return expandTargetRectangle(new Rect2d(x_min, y_min, x_max - x_min, y_max - y_min), factor);
  }

  @NonNull
  private Rect expandTargetRectangle(@NonNull Rect2d rect, float factor) {
    double x_min = rect.x;
    double y_min = rect.y;
    double w = rect.width;
    double h = rect.height;

    Rect targetRect = new Rect();
    targetRect.x = (int)(x_min - w * factor);
    targetRect.y = (int)(y_min - h * factor);
    targetRect.width = (int)(w * (factor * 2 + 1) + 0.5f);
    targetRect.height = (int)(h * (factor * 2 + 1) + 0.5f);

    if (targetRect.width > targetRect.height) {
      targetRect.y -= (targetRect.width - targetRect.height) / 2;
      targetRect.height = targetRect.width;
    }
    else {
      targetRect.x -= (targetRect.height - targetRect.width) / 2;
      targetRect.width = targetRect.height;
    }

    return targetRect;
  }

  @NonNull
  Rect getValidRect(int cols, int rows, Rect targetRect) {
    Rect validRect = new Rect();
    int x0 = targetRect.x;
    int x1 = targetRect.x + targetRect.width;
    int y0 = targetRect.y;
    int y1 = targetRect.y + targetRect.height;
    x0 = Math.max(x0, 0);
    x1 = Math.min(x1, cols);
    y0 = Math.max(y0, 0);
    y1 = Math.min(y1, rows);
    validRect.x = x0;
    validRect.y = y0;
    validRect.width = x1 - x0;
    validRect.height = y1 - y0;
    return validRect;
  }
}

package com.realsenseai.rsid.sample.callbacks;

import android.graphics.Bitmap;
import androidx.annotation.NonNull;

public interface RealSenseIdPreviewCallback {
  void onPreviewBitmapReady(@NonNull Bitmap bitmap);
}

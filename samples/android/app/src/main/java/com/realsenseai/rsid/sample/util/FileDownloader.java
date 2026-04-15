// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util;

import android.os.Environment;
import androidx.annotation.NonNull;
import androidx.lifecycle.MutableLiveData;
import java.io.File;
import java.io.IOException;
import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okio.Okio;
import timber.log.Timber;

public class FileDownloader {

  public static final String REALSENSE_ID_FOLDER = "RealSenseID";
  private final MutableLiveData<Integer> progressUpdate;
  private final OkHttpClient okHttpClient;
  private final File folder;
  private final String TAG = "FileDownloader";

  public FileDownloader(MutableLiveData<Integer> progressUpdate) {
    this.progressUpdate = progressUpdate;
    okHttpClient = new OkHttpClient();
    folder = new File(Environment
                        .getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), REALSENSE_ID_FOLDER);
  }

  public void downloadFile(@NonNull String downloadUrl, @NonNull String filename,
                           @NonNull FileDownloaderCallback downloaderCallback) {
    if (!folder.exists() && !folder.mkdir()) {
      Timber.tag(TAG).e("FirmwareDownloader can't create download folder!");
      downloaderCallback.onDownloadFailure("Unable to create download folder");
      return;
    }

    var downloadFile = new File(folder + "/" + filename);
    if (downloadFile.exists()) {
      downloadFile.delete();
    }

    okHttpClient.newCall(new Request.Builder().url(downloadUrl).tag(filename).build()).enqueue(new Callback() {
      @Override
      public void onFailure(@NonNull Call call, @NonNull IOException e) {
        Timber.tag(TAG).e(e);
        downloaderCallback.onDownloadFailure(e.getMessage());
      }

      @Override
      public void onResponse(@NonNull Call call, @NonNull Response response) {
        if (response.isSuccessful()) {
          try {
            var responseBody = response.body();
            assert responseBody != null;
            var totalSize = responseBody.contentLength();
            var source = responseBody.source();
            var file = new File(folder + "/" + filename);
            var sink = Okio.buffer(Okio.sink(file));
            var bufferSize = 8 * 1024;
            long currentSize = 0;
            for (long bytesRead; (bytesRead = source.read(sink.getBuffer(), bufferSize)) != -1; ) {
              sink.emit();
              currentSize += bytesRead;
              progressUpdate.postValue((int)Math.floorDiv(currentSize * 100, totalSize));
            }
            sink.flush();
            sink.close();
            source.close();
            Timber.tag(TAG).d("%s successfully downloaded and saved to storage.", filename);
            downloaderCallback.onDownloadSuccess(file.getAbsolutePath());
          }
          catch (IOException e) {
            Timber.tag(TAG).e(e);
            downloaderCallback.onDownloadFailure(e.getMessage());
          }
        }
      }
    });
  }

  public interface FileDownloaderCallback {
    void onDownloadSuccess(String filePath);

    void onDownloadFailure(String message);
  }
}

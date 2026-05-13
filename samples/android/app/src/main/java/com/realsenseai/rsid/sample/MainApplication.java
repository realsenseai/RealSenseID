// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample;

import android.app.Application;
import android.os.StrictMode;
import com.realsenseai.rsid.api.RealSenseID;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import timber.log.Timber;


public class MainApplication extends Application {

  private static volatile MainApplication sInstance;

  public static MainApplication getInstance() {
    return sInstance;
  }

  @Override
  public void onCreate() {
    super.onCreate();
    sInstance = this;
    initializeComponents();
  }

  private void initializeComponents() {
    initializeLogging();

    System.loadLibrary("RealSenseIDSwigJNI");
    RealSenseID.SetAssetManager(getAssets());
    SDKWrapper.INSTANCE.init(this);
  }

  private void initializeLogging() {

    /*
    if (BuildConfig.DEBUG) {
      Timber.plant(new Timber.DebugTree());
    }
     */
    Timber.plant(new Timber.DebugTree());
  }

  private void enableStrictMode() {
    StrictMode.setThreadPolicy(new StrictMode.ThreadPolicy.Builder()
                                 .detectDiskReads()
                                 .detectDiskWrites()
                                 .detectNetwork()
                                 .penaltyLog()
                                 .build());

    StrictMode.setVmPolicy(new StrictMode.VmPolicy.Builder()
                             .detectLeakedSqlLiteObjects()
                             .detectLeakedClosableObjects()
                             .penaltyLog()
                             .build());
  }
}

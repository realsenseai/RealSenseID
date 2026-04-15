// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample;

import android.app.Application;
import android.content.pm.ApplicationInfo;
import android.os.StrictMode;
import timber.log.Timber;


public class MainApplication extends Application {

  private static volatile MainApplication sInstance;
  private boolean isDebuggable;

  public static MainApplication getInstance() {
    return sInstance;
  }

  @Override
  public void onCreate() {
    super.onCreate();
    sInstance = this;
    isDebuggable = (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;

    initializeComponents();
  }

  private void initializeComponents() {
    initializeLogging();

    //if (isDebuggable) {
    //  enableStrictMode();
    //}
  }

  private void initializeLogging() {

    if (BuildConfig.DEBUG) {
      Timber.plant(new Timber.DebugTree());
    }
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

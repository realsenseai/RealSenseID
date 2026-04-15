// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.settings;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;

public class SettingsViewModel extends ViewModel {

  private final MutableLiveData<Settings> settings;

  public SettingsViewModel() {
    settings = new MutableLiveData<>();
    settings.setValue(new Settings());
  }

  public LiveData<Settings> getSettings() {
    return settings;
  }

  public enum DumpMode {
    NONE, FACE, FULL
  }

  // private final MutableLiveData<>

  public enum SecurityLevel {
    HIGH, MEDIUM, STANDARD
  }

  public static class Settings {
    public DumpMode dumpMode;
    public SecurityLevel securityLevel;
  }
}
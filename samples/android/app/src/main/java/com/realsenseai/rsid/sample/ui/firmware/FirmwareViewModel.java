// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.firmware;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;

public class FirmwareViewModel extends ViewModel {

  private final MutableLiveData<String> mVersionText;

  public FirmwareViewModel() {
    mVersionText = new MutableLiveData<>();
  }

  public LiveData<String> getText() {
    return mVersionText;
  }

  public void setText(String text) {
    mVersionText.setValue(text);
  }
}
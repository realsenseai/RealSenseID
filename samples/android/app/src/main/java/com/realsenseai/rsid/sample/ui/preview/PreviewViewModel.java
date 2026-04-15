// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.preview;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;

public class PreviewViewModel extends ViewModel {
  private final MutableLiveData<Boolean> isCompatible = new MutableLiveData<>(false);

  public LiveData<Boolean> getIsCompatible() {
    return isCompatible;
  }

  public void setIsCompatible(boolean compatible) {
    isCompatible.postValue(compatible);
  }

  public boolean isDeviceCompatible() {
    Boolean value = isCompatible.getValue();
    return value != null && value;
  }

  @Override
  protected void onCleared() {
    super.onCleared();
  }
}
// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.callbacks;

public interface EnrollmentCallback {
  void onEnrollmentSuccess(String name);

  void onEnrollmentFailure(String status);

  void onEnrollmentHint(String hint);
}

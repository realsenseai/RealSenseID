// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.db;

import androidx.room.TypeConverter;
import com.google.gson.Gson;

public class Converters {
  @TypeConverter
  public short[] deserialize(String json) {
    return new Gson().fromJson(json, short[].class);
  }

  @TypeConverter
  public String serialize(short[] vector) {
    return new Gson().toJson(vector);
  }
}

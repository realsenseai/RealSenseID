// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.db;

import androidx.annotation.NonNull;
import androidx.room.ColumnInfo;
import androidx.room.Entity;
import androidx.room.PrimaryKey;
import java.util.Arrays;

@Entity(tableName = ("user"))
public class User {
  @PrimaryKey(autoGenerate = true)
  @ColumnInfo(name = "id")
  public Integer id;

  @ColumnInfo(name = "name")
  public String name;

  @ColumnInfo(name = "enrollment_descriptor")
  public short[] enrollmentDescriptor;

  @ColumnInfo(name = "adaptive_descriptor_with_mask")
  public short[] adaptiveDescriptorWithMask;

  @ColumnInfo(name = "adaptive_descriptor_without_mask")
  public short[] adaptiveDescriptorWithoutMask;

  @ColumnInfo(name = "flags")
  public int flags;

  @ColumnInfo(name = "features_type")
  public int featuresType;

  @ColumnInfo(name = "version")
  public int version;

  public User(String name,
              short[] enrollmentDescriptor,
              short[] adaptiveDescriptorWithMask,
              short[] adaptiveDescriptorWithoutMask,
              int flags,
              int featuresType,
              int version) {
    this.name = name;
    this.enrollmentDescriptor = enrollmentDescriptor;
    this.adaptiveDescriptorWithMask = adaptiveDescriptorWithMask;
    this.adaptiveDescriptorWithoutMask = adaptiveDescriptorWithoutMask;
    this.flags = flags;
    this.featuresType = featuresType;
    this.version = version;
  }

  public String getName() {
    return name;
  }

  public void setName(String name) {
    this.name = name;
  }

  public short[] getEnrollmentDescriptor() {
    return enrollmentDescriptor;
  }

  public void setEnrollmentDescriptor(short[] enrollmentDescriptor) {
    this.enrollmentDescriptor = enrollmentDescriptor;
  }

  public short[] getAdaptiveDescriptorWithMask() {
    return adaptiveDescriptorWithMask;
  }

  public void setAdaptiveDescriptorWithMask(short[] adaptiveDescriptorWithMask) {
    this.adaptiveDescriptorWithMask = adaptiveDescriptorWithMask;
  }

  public short[] getAdaptiveDescriptorWithoutMask() {
    return adaptiveDescriptorWithoutMask;
  }

  public void setAdaptiveDescriptorWithoutMask(short[] adaptiveDescriptorWithoutMask) {
    this.adaptiveDescriptorWithoutMask = adaptiveDescriptorWithoutMask;
  }

  public int getFlags() {
    return flags;
  }

  public void setFlags(int flags) {
    this.flags = flags;
  }

  public int getFeaturesType() {
    return featuresType;
  }

  public void setFeaturesType(int featuresType) {
    this.featuresType = featuresType;
  }

  public int getVersion() {
    return version;
  }

  public void setVersion(int version) {
    this.version = version;
  }

  @NonNull
  @Override
  public String toString() {
    return "User{" +
           "id=" + id +
           ", name='" + name + '\'' +
           ", enrollmentDescriptor=" + Arrays.toString(enrollmentDescriptor) +
           ", adaptiveDescriptorWithMask=" + Arrays.toString(adaptiveDescriptorWithMask) +
           ", adaptiveDescriptorWithoutMask=" + Arrays.toString(adaptiveDescriptorWithoutMask) +
           ", flags=" + flags +
           ", featuresType=" + featuresType +
           ", version=" + version +
           '}';
  }

  public static class Builder {
    private String name;
    private short[] enrollmentDescriptor;
    private short[] adaptiveDescriptorWithMask;
    private short[] adaptiveDescriptorWithoutMask;
    private int flags;
    private int featuresType;
    private int version;

    public Builder setName(String name) {
      this.name = name;
      return this;
    }

    public Builder setEnrollmentDescriptor(short[] enrollmentDescriptor) {
      this.enrollmentDescriptor = enrollmentDescriptor;
      return this;
    }

    public Builder setAdaptiveDescriptorWithMask(short[] adaptiveDescriptorWithMask) {
      this.adaptiveDescriptorWithMask = adaptiveDescriptorWithMask;
      return this;
    }

    public Builder setAdaptiveDescriptorWithoutMask(short[] adaptiveDescriptorWithoutMask) {
      this.adaptiveDescriptorWithoutMask = adaptiveDescriptorWithoutMask;
      return this;
    }

    public Builder setFlags(int flags) {
      this.flags = flags;
      return this;
    }

    public Builder setFeaturesType(int featuresType) {
      this.featuresType = featuresType;
      return this;
    }

    public Builder setVersion(int version) {
      this.version = version;
      return this;
    }

    public User build() {
      return new User(name,
                      enrollmentDescriptor,
                      adaptiveDescriptorWithMask,
                      adaptiveDescriptorWithoutMask,
                      flags,
                      featuresType,
                      version);
    }
  }
}

package com.realsenseai.rsid.sample.ui.users;

import androidx.annotation.NonNull;

public class DeviceUser {
  private String name;

  public DeviceUser(String name) {
    this.name = name;
  }

  public String getName() {
    return name;
  }

  public void setName(String name) {
    this.name = name;
  }

  @NonNull
  @Override
  public String toString() {
    return name;
  }
}

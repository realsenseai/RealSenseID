package com.realsenseai.rsid.sample.util;

import static java.util.Objects.isNull;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Parcelable;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.realsenseai.rsid.api.DeviceType;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class UsbDevicesReceiver extends BroadcastReceiver {

  private final UsbListener usbListener;

  public UsbDevicesReceiver(@NonNull UsbListener usbListener) {
    this.usbListener = usbListener;
  }

  @NonNull
  public List<UsbDevice> getAllAttachedDevices(Context context) {
    if (context == null) {
      return Collections.emptyList();
    }
    UsbManager usbManager = (UsbManager)context.getSystemService(Context.USB_SERVICE);
    if (usbManager != null) {
      return new ArrayList<>(usbManager.getDeviceList().values());
    }
    return Collections.emptyList();
  }

  @NonNull
  public DeviceType getAttachedDeviceType(@NonNull Context context) {
    // TODO: Refactor and use getFirstRealSenseIdDevice instead of looping
    List<UsbDevice> attachedDevices = getAllAttachedDevices(context);

    if (attachedDevices.isEmpty()) {
      return DeviceType.Unknown; // No devices attached
    }

    for (UsbDevice device : attachedDevices) {
      int vid = device.getVendorId();
      int pid = device.getProductId();

      // F45x Product IDs and Vendor IDs
      if ((vid == 1240 && pid == 221) || (vid == 10925 && pid == 25459)) {
        return DeviceType.F45x;
      }
      // F50x Product IDs and Vendor IDs
      else if ((vid == 16716 && pid == 25976) || (vid == 16716 && pid == 26214)) {
        return DeviceType.F50x;
      }
    }
    return DeviceType.Unknown;
  }

  @Nullable
  public UsbDevice getFirstRealSenseIdDevice(@NonNull Context context) {
    List<UsbDevice> attachedDevices = getAllAttachedDevices(context);

    if (attachedDevices.isEmpty()) {
      return null; // No devices attached
    }

    for (UsbDevice device : attachedDevices) {
      int vid = device.getVendorId();
      int pid = device.getProductId();

      // F45x Product IDs and Vendor IDs
      if ((vid == 1240 && pid == 221) || (vid == 10925 && pid == 25459)) {
        return device;
      }
      // F50x Product IDs and Vendor IDs
      else if ((vid == 16716 && pid == 25976) || (vid == 16716 && pid == 26214)) {
        return device;
      }
    }
    return null;
  }

  public boolean isRealSenseIdDeviceAttached(Context context) {
    List<UsbDevice> attachedDevices = getAllAttachedDevices(context);
    if (attachedDevices.isEmpty()) {
      return false;
    }

    for (UsbDevice device : attachedDevices) {
      if (isSupportedDevice(device)) {
        return true;
      }
    }
    return false;
  }

  @Override
  public void onReceive(Context context, Intent intent) {
    String action = intent.getAction();

    if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
      Parcelable parcelable = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
      UsbDevice usbDevice = parcelable instanceof UsbDevice ? (UsbDevice)parcelable : null;

      if (isSupportedDevice(usbDevice)) {
        usbListener.onDeviceDetached(usbDevice);
      }
    }

    if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)) {
      Parcelable parcelable = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
      UsbDevice usbDevice = parcelable instanceof UsbDevice ? (UsbDevice)parcelable : null;

      if (isSupportedDevice(usbDevice)) {
        usbListener.onDeviceAttached(usbDevice);
      }
    }
  }

  private boolean isSupportedDevice(@Nullable UsbDevice usbDevice) {
    if (isNull(usbDevice)) {
      return false;
    }

    List<int[]> supportedDevices = List.of(
      // Each inner array: {vendorId, productId}
      new int[]{1240, 221},    // F45x - 04d8:00dd
      new int[]{10925, 25459}, // F45x - 2aad:6373
      new int[]{16716, 25976}, // F50x - 414c:6578
      new int[]{16716, 26214}  // F50x - 414c:6666
    );

    assert usbDevice != null;
    int deviceVendorId = usbDevice.getVendorId();
    int deviceProductId = usbDevice.getProductId();

    for (int[] ids : supportedDevices) {
      if (ids.length == 2 && ids[0] == deviceVendorId && ids[1] == deviceProductId) {
        return true;
      }
    }

    return false;
  }

  public interface UsbListener {
    void onDeviceDetached(@Nullable UsbDevice usbDevice);

    void onDeviceAttached(@Nullable UsbDevice usbDevice);
  }
}

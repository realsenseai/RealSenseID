package com.realsenseai.rsid.impl;

import static android.hardware.usb.UsbConstants.USB_CLASS_CDC_DATA;
import static android.hardware.usb.UsbConstants.USB_CLASS_COMM;
import static android.hardware.usb.UsbConstants.USB_DIR_IN;
import static android.hardware.usb.UsbConstants.USB_DIR_OUT;
import static android.hardware.usb.UsbConstants.USB_ENDPOINT_XFER_BULK;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import androidx.core.content.ContextCompat;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

public class UsbCdcConnection
{
    private static final String TAG = "UsbCdcConnection";
    private static final long PERMISSION_TIMEOUT_MS = 15000; // 15 seconds timeout for permission dialog
    private static final List<UsbCdcDeviceInformation> SUPPORTED_DEVICES = Arrays.asList(new UsbCdcDeviceInformation
                                                                                             .Builder() // F45x - 04d8:00dd
                                                                                             .setDeviceVendorId(1240)
                                                                                             .setProductId(221)
                                                                                             .build(),
                                                                                         new UsbCdcDeviceInformation
                                                                                             .Builder() // F45x - 2aad:6373
                                                                                             .setDeviceVendorId(10925)
                                                                                             .setProductId(25459)
                                                                                             .build(),
                                                                                         new UsbCdcDeviceInformation
                                                                                             .Builder() // F50x - 414c:6578
                                                                                             .setDeviceVendorId(16716)
                                                                                             .setProductId(25976)
                                                                                             .build(),
                                                                                         new UsbCdcDeviceInformation
                                                                                             .Builder() // F50x - 414c:6666
                                                                                             .setDeviceVendorId(16716)
                                                                                             .setProductId(26214)
                                                                                             .build());
    private final UsbCdcConnectionDetails usbCdcConnectionDetails = new UsbCdcConnectionDetails();
    private final UvcCameraInfo uvcCameraInfo = new UvcCameraInfo();
    private boolean isConnectionOpen = false;

    public UsbCdcConnection()
    {
    }

    private boolean FindUvcInterfaces()
    {
        if (usbCdcConnectionDetails.usbDevice == null)
        {
            return false;
        }

        UsbInterface videoControlInterface = null;
        UsbInterface videoStreamingInterface = null;

        // Look for UVC interfaces on the same device
        for (int i = 0; i < usbCdcConnectionDetails.usbDevice.getInterfaceCount(); i++)
        {
            UsbInterface usbInterface = usbCdcConnectionDetails.usbDevice.getInterface(i);

            // UVC Video Control Interface Class
            if (usbInterface.getInterfaceClass() == 14 && usbInterface.getInterfaceSubclass() == 1)
            {
                Log.d(TAG, "Found UVC Video Control interface at index " + i);
                videoControlInterface = usbInterface;
            }

            // UVC Video Streaming Interface Class
            if (usbInterface.getInterfaceClass() == 14 && usbInterface.getInterfaceSubclass() == 2)
            {
                Log.d(TAG, "Found UVC Video Streaming interface at index " + i);
                videoStreamingInterface = usbInterface;
            }
        }

        if (videoControlInterface != null && videoStreamingInterface != null)
        {
            uvcCameraInfo.videoControlInterface = videoControlInterface;
            uvcCameraInfo.videoStreamingInterface = videoStreamingInterface;
            return true;
        }

        return false;
    }

    public int GetReadEndpointAddress()
    {
        if (!isConnectionOpen)
        {
            Log.e(TAG, "Device connection is not open");
            return -1;
        }

        return usbCdcConnectionDetails.readEndpointAddress;
    }

    public int GetWriteEndpointAddress()
    {
        if (!isConnectionOpen)
        {
            Log.e(TAG, "Device connection is not open");
            return -1;
        }

        return usbCdcConnectionDetails.writeEndpointAddress;
    }

    public int GetFileDescriptor()
    {
        if (!isConnectionOpen)
        {
            Log.e(TAG, "Device connection is not open - cannot get file descriptor");
            return -1;
        }
        if (usbCdcConnectionDetails.fileDescriptor < 0)
        {
            Log.e(TAG, "File descriptor is invalid: " + usbCdcConnectionDetails.fileDescriptor);
            return -1;
        }
        return usbCdcConnectionDetails.fileDescriptor;
    }

    public boolean FindSupportedDevice(Context context)
    {
        var usbDevicesList = GetUsbDevicesList(context);
        if (usbDevicesList == null || usbDevicesList.isEmpty())
        {
            Log.i(TAG, "USB Devices List is Empty");
            return false;
        }

        if (!FindUsbDevice(usbDevicesList))
        {
            Log.e(TAG, "Requested USB device was not found");
            PrintAllDevicesDetails(usbDevicesList);
            return false;
        }
        return true;
    }

    public boolean hasPermissionForDevice(Context context)
    {
        if (usbCdcConnectionDetails.usbDevice == null)
        {
            return false;
        }

        UsbManager usbManager = (UsbManager)context.getSystemService(Context.USB_SERVICE);
        return usbManager != null && usbManager.hasPermission(usbCdcConnectionDetails.usbDevice);
    }

    public void RequestDevicePermission(Context context, final PermissionCallback callback)
    {
        if (hasPermissionForDevice(context))
        {
            Log.d(TAG, "Device already has permission - no dialog needed");
            callback.Response(true);
            return;
        }

        final var ACTION_USB_PERMISSION = "com.realsense.rsid.USB_PERMISSION";
        int flags = 0;
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M)
        {
            flags = PendingIntent.FLAG_IMMUTABLE;
        }
        var permissionIntent = PendingIntent.getBroadcast(context, 0, new Intent(ACTION_USB_PERMISSION), flags);
        var filter = new IntentFilter(ACTION_USB_PERMISSION);

        // Use AtomicBoolean to track if callback has been invoked to prevent double-callback
        final AtomicBoolean callbackInvoked = new AtomicBoolean(false);

        // Create handler for timeout
        final Handler timeoutHandler = new Handler(Looper.getMainLooper());

        var usbPermissionReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent)
            {
                var action = intent.getAction();
                if (ACTION_USB_PERMISSION.equals(action))
                {
                    // Only proceed if callback hasn't been invoked yet
                    if (!callbackInvoked.compareAndSet(false, true))
                    {
                        Log.w(TAG, "Permission response received but callback already invoked (likely timed out)");
                        return;
                    }

                    // Cancel the timeout since we received a response
                    timeoutHandler.removeCallbacksAndMessages(null);

                    // Unregister the receiver immediately to prevent memory leak
                    try
                    {
                        context.unregisterReceiver(this);
                    }
                    catch (IllegalArgumentException e)
                    {
                        // Receiver was already unregistered, ignore
                        Log.w(TAG, "Receiver was already unregistered");
                    }

                    var device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                    if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false) && (device != null))
                    {
                        if (device.equals(usbCdcConnectionDetails.usbDevice))
                        {
                            Log.d(TAG, "GRANTED permission to access USB device");
                            callback.Response(true);
                        }
                        else
                        {
                            Log.w(TAG, "Permission granted to a different USB device");
                            callback.Response(false);
                        }
                    }
                    else
                    {
                        Log.e(TAG, "DENIED permission to access USB device");
                        callback.Response(false);
                    }
                }
            }
        };

        ContextCompat.registerReceiver(context, usbPermissionReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED);

        // Schedule timeout
        timeoutHandler.postDelayed(() -> {
            // Only proceed if callback hasn't been invoked yet
            if (!callbackInvoked.compareAndSet(false, true))
            {
                // Callback was already invoked by the receiver
                return;
            }

            Log.e(TAG, "USB permission request timed out after " + PERMISSION_TIMEOUT_MS + "ms");

            // Unregister the receiver on timeout
            try
            {
                context.unregisterReceiver(usbPermissionReceiver);
                Log.d(TAG, "Unregistered permission receiver after timeout");
            }
            catch (IllegalArgumentException e)
            {
                Log.w(TAG, "Receiver was already unregistered on timeout: " + e.getMessage());
            }

            // Invoke callback with failure
            callback.Response(false);
        }, PERMISSION_TIMEOUT_MS);

        // Request permission - we already checked that we don't have it at line 146
        Log.d(TAG, "Requesting USB permission from user (timeout: " + PERMISSION_TIMEOUT_MS + "ms)");
        usbCdcConnectionDetails.usbManager.requestPermission(usbCdcConnectionDetails.usbDevice, permissionIntent);
    }

    public boolean OpenConnection()
    {
        if (isConnectionOpen)
        {
            Log.i(TAG, "✨ Device connection already open");
            return true;
        }

        if (!OpenConnectionWithTheDevice())
        {
            Log.e(TAG, "⛔ Failed to open device connection");
            // Clean up any partial state
            usbCdcConnectionDetails.ResetConnectionDetails();
            return false;
        }

        if (!GetDeviceInterfaces())
        {
            Log.e(TAG, "⛔ Error reaching the device interfaces");
            // Clean up any partial state
            usbCdcConnectionDetails.ResetConnectionDetails();
            return false;
        }

        /*  // TODO: This isn't really needed!
        if (!ConfigureTheConnection()) {
          Log.e(TAG, "⛔ Failed to configure device connection");
          // Clean up any partial state
          usbCdcConnectionDetails.ResetConnectionDetails();
          return false;
        }
        */

        Log.i(TAG, "✨ Device connection established successfully");
        isConnectionOpen = true;
        return true;
    }

    public void CloseConnection()
    {
        if (isConnectionOpen)
        {
            Log.i(TAG, "❌ Closing USB CDC connection");
            usbCdcConnectionDetails.ResetConnectionDetails();
            isConnectionOpen = false;
            Log.i(TAG, "❌ USB CDC connection closed successfully");
        }
        else
        {
            Log.i(TAG, "❌ Connection is already closed");
        }
    }

    private boolean ConfigureTheConnection()
    {
        byte[] configurationBuffer = {(byte)(UsbCdcConnectionConstants.BAUD_RATE & 0xff),
                                      (byte)((UsbCdcConnectionConstants.BAUD_RATE >> 8) & 0xff),
                                      (byte)((UsbCdcConnectionConstants.BAUD_RATE >> 16) & 0xff),
                                      (byte)((UsbCdcConnectionConstants.BAUD_RATE >> 24) & 0xff),
                                      UsbCdcConnectionConstants.STOP_BITS,
                                      UsbCdcConnectionConstants.PARITY,
                                      UsbCdcConnectionConstants.DATA_BITS};

        final int SET_LINE_CODING = 0x20;
        final int USB_INTERFACE_REQUEST_TYPE = 0x21;
        // 0x21 --> 100001 --> 3 fields
        //           0                  01              00001
        //           Host to Device     Type == Class   Recipient == Interface

        final int USB_ENDPOINT_REQUEST_TYPE = 0x22;
        // 0x21 --> 100001 --> 3 fields
        //           0                  01              00010
        //           Host to Device     Type == Class   Recipient == Interface

        int returnValue = usbCdcConnectionDetails.usbDeviceConnection.controlTransfer(
            USB_INTERFACE_REQUEST_TYPE, SET_LINE_CODING, 0, usbCdcConnectionDetails.controlInterfaceIndex, configurationBuffer,
            configurationBuffer.length, 5000);
        if (-1 == returnValue)
        {
            Log.e(TAG, "Failed configure the connection with the device");
            return false;
        }
        return true;
    }

    private boolean OpenConnectionWithTheDevice()
    {
        var usbDeviceConnection = usbCdcConnectionDetails.usbManager.openDevice(usbCdcConnectionDetails.usbDevice);
        if (usbDeviceConnection == null)
        {
            return false;
        }

        usbCdcConnectionDetails.usbDeviceConnection = usbDeviceConnection;
        usbCdcConnectionDetails.fileDescriptor = usbDeviceConnection.getFileDescriptor();
        return true;
    }

    private boolean GetDeviceInterfaces()
    {
        if (null == usbCdcConnectionDetails.usbDeviceConnection)
        {
            Log.e(TAG, "Null device connection");
            return false;
        }
        if (null == usbCdcConnectionDetails.usbDevice)
        {
            Log.e(TAG, "Null USB device");
            return false;
        }

        UsbInterface controlInterface = null;
        int controlInterfaceIndex = -1;
        int controlEndpointAddress = -1;
        UsbInterface dataInterface = null;
        int readEndpointAddress = -1;
        int writeEndpointAddress = -1;

        for (int interfaceIndex = 0; interfaceIndex < usbCdcConnectionDetails.usbDevice.getInterfaceCount(); ++interfaceIndex)
        {
            var usbInterface = usbCdcConnectionDetails.usbDevice.getInterface(interfaceIndex);

            if (USB_CLASS_COMM == usbInterface.getInterfaceClass())
            {
                Log.d(TAG, "Found CDC control interface");
                controlInterface = usbInterface;
                controlInterfaceIndex = interfaceIndex;
                UsbEndpoint controlEndpoint = usbInterface.getEndpoint(0);
                controlEndpointAddress = controlEndpoint.getAddress();
                continue;
            }

            if (USB_CLASS_CDC_DATA == usbInterface.getInterfaceClass())
            {
                Log.d(TAG, "Found data interface");
                int numberOfEndpoints = usbInterface.getEndpointCount();
                Log.d(TAG, "This interface has " + numberOfEndpoints + " endpoints");
                for (int endpointIndex = 0; endpointIndex < numberOfEndpoints; ++endpointIndex)
                {
                    UsbEndpoint usbEndpoint = usbInterface.getEndpoint(endpointIndex);

                    if (USB_ENDPOINT_XFER_BULK == usbEndpoint.getType())
                    {
                        Log.d(TAG, "Found BULK endpoint");
                        dataInterface = usbInterface;
                        if (USB_DIR_IN == usbEndpoint.getDirection())
                        {
                            readEndpointAddress = usbEndpoint.getAddress();
                        }
                        if (USB_DIR_OUT == usbEndpoint.getDirection())
                        {
                            writeEndpointAddress = usbEndpoint.getAddress();
                        }
                    }
                }
            }
        }

        if (readEndpointAddress != -1 && writeEndpointAddress != -1 && controlEndpointAddress != -1)
        {
            if (!usbCdcConnectionDetails.usbDeviceConnection.claimInterface(controlInterface, true))
            {
                Log.e(TAG, "Failed to claim the control interface");
                return false;
            }
            else
            {
                Log.d(TAG, "Control interface claimed successfully");
            }

            if (!usbCdcConnectionDetails.usbDeviceConnection.claimInterface(dataInterface, true))
            {
                Log.e(TAG, "Failed to claim the data interface");
                // Release the already claimed control interface
                usbCdcConnectionDetails.usbDeviceConnection.releaseInterface(controlInterface);
                return false;
            }
            else
            {
                Log.d(TAG, "Data interface claimed successfully");
            }

            usbCdcConnectionDetails.controlInterface = controlInterface;
            usbCdcConnectionDetails.controlInterfaceIndex = controlInterfaceIndex;
            usbCdcConnectionDetails.controlEndpointAddress = controlEndpointAddress;
            usbCdcConnectionDetails.dataInterface = dataInterface;
            usbCdcConnectionDetails.readEndpointAddress = readEndpointAddress;
            usbCdcConnectionDetails.writeEndpointAddress = writeEndpointAddress;
            return true;
        }

        return false;
    }

    private HashMap<String, UsbDevice> GetUsbDevicesList(Context context)
    {
        HashMap<String, UsbDevice> usbDevicesList = null;

        usbCdcConnectionDetails.usbManager = (UsbManager)context.getSystemService(Context.USB_SERVICE);
        if (usbCdcConnectionDetails.usbManager == null)
        {
            Log.e(TAG, "Error getting the usbManager");
        }
        else
        {
            usbDevicesList = usbCdcConnectionDetails.usbManager.getDeviceList();
        }

        return usbDevicesList;
    }

    private boolean FindUsbDevice(HashMap<String, UsbDevice> usbDevicesList)
    {
        boolean bDeviceWasFound = false;

        var devicesIterator = usbDevicesList.values().iterator();

        while (!bDeviceWasFound && devicesIterator.hasNext())
        {
            var usbDevice = devicesIterator.next();

            for (UsbCdcDeviceInformation usbCdcDeviceInformation : SUPPORTED_DEVICES)
            {
                if (usbDevice.getVendorId() == usbCdcDeviceInformation.getDeviceVendorId() &&
                    usbDevice.getProductId() == usbCdcDeviceInformation.getProductId())
                {
                    bDeviceWasFound = true;
                    usbCdcConnectionDetails.usbDevice = usbDevice;
                    Log.i(TAG, "The requested USB device was found");
                    PrintDeviceDetails(usbDevice);
                    break;
                }
            }
        }

        return bDeviceWasFound;
    }

    private void PrintDeviceDetails(UsbDevice usbDevice)
    {
        if (null != usbDevice)
        {
            Log.i(TAG, usbDevice.toString());
        }
    }

    private void PrintAllDevicesDetails(HashMap<String, UsbDevice> usbDevicesList)
    {
        var devicesIterator = usbDevicesList.values().iterator();

        Log.i(TAG, "The following USB devices were detected");
        while (devicesIterator.hasNext())
        {
            var usbDevice = devicesIterator.next();
            Log.i(TAG, usbDevice.toString());
        }
    }

    public boolean OpenUvcConnection()
    {
        if (!FindUvcInterfaces())
        {
            Log.e(TAG, "UVC interfaces not found on this device");
            return false;
        }

        if (uvcCameraInfo.uvcConnection != null)
        {
            Log.e(TAG, "UVC interfaces is already open");
            return true;
        }

        // Create a SEPARATE USB connection for UVC

        uvcCameraInfo.uvcConnection = usbCdcConnectionDetails.usbManager.openDevice(usbCdcConnectionDetails.usbDevice);
        if (uvcCameraInfo.uvcConnection == null)
        {
            Log.e(TAG, "Failed to open UVC connection to device");
            return false;
        }

        // Claim the UVC interfaces
        if (!uvcCameraInfo.uvcConnection.claimInterface(uvcCameraInfo.videoControlInterface, true))
        {
            Log.e(TAG, "Failed to claim UVC control interface");
            uvcCameraInfo.uvcConnection.close();
            uvcCameraInfo.uvcConnection = null;
            return false;
        }

        if (!uvcCameraInfo.uvcConnection.claimInterface(uvcCameraInfo.videoStreamingInterface, true))
        {
            Log.e(TAG, "Failed to claim UVC streaming interface");
            uvcCameraInfo.uvcConnection.releaseInterface(uvcCameraInfo.videoControlInterface);
            uvcCameraInfo.uvcConnection.close();
            uvcCameraInfo.uvcConnection = null;
            return false;
        }

        // Get the file descriptor for the UVC connection
        uvcCameraInfo.fileDescriptor = uvcCameraInfo.uvcConnection.getFileDescriptor();

        Log.i(TAG, "UVC camera connection opened successfully. FD: " + uvcCameraInfo.fileDescriptor);
        return true;
    }

    public int GetUvcFileDescriptor()
    {
        return uvcCameraInfo.fileDescriptor;
    }

    public void CloseUvcConnection()
    {
        if (uvcCameraInfo.uvcConnection != null)
        {
            if (uvcCameraInfo.videoControlInterface != null)
            {
                uvcCameraInfo.uvcConnection.releaseInterface(uvcCameraInfo.videoControlInterface);
            }
            if (uvcCameraInfo.videoStreamingInterface != null)
            {
                uvcCameraInfo.uvcConnection.releaseInterface(uvcCameraInfo.videoStreamingInterface);
            }
            uvcCameraInfo.uvcConnection.close();
            uvcCameraInfo.uvcConnection = null;
            uvcCameraInfo.fileDescriptor = -1;
        }
    }

    public interface PermissionCallback
    {
        void Response(boolean permissionGranted);
    }

    static class UvcCameraInfo
    {
        public UsbInterface videoControlInterface;
        public UsbInterface videoStreamingInterface;
        public int fileDescriptor = -1;
        public UsbDeviceConnection uvcConnection;
    }

    static class UsbCdcConnectionDetails
    {
        public UsbInterface controlInterface = null;
        public int controlInterfaceIndex = -1;
        public int controlEndpointAddress = -1;
        public UsbInterface dataInterface = null;
        public int readEndpointAddress = -1;
        public int writeEndpointAddress = -1;
        public int fileDescriptor = -1;
        public UsbDeviceConnection usbDeviceConnection = null;
        public UsbManager usbManager = null;
        public UsbDevice usbDevice = null;

        public void ResetConnectionDetails()
        {
            // Release claimed interfaces before closing connection
            if (null != usbDeviceConnection)
            {
                if (null != controlInterface)
                {
                    try
                    {
                        usbDeviceConnection.releaseInterface(controlInterface);
                        Log.d(TAG, "Released control interface");
                    }
                    catch (Exception e)
                    {
                        Log.w(TAG, "Failed to release control interface: " + e.getMessage());
                    }
                }
                if (null != dataInterface)
                {
                    try
                    {
                        usbDeviceConnection.releaseInterface(dataInterface);
                        Log.d(TAG, "Released data interface");
                    }
                    catch (Exception e)
                    {
                        Log.w(TAG, "Failed to release data interface: " + e.getMessage());
                    }
                }

                // Close the USB device connection (this should close the file descriptor)
                try
                {
                    usbDeviceConnection.close();
                    Log.d(TAG, "Closed USB device connection and file descriptor");
                }
                catch (Exception e)
                {
                    Log.w(TAG, "Failed to close USB device connection: " + e.getMessage());
                }
                usbDeviceConnection = null;
            }

            // Reset all connection details
            controlInterface = null;
            controlInterfaceIndex = -1;
            controlEndpointAddress = -1;

            dataInterface = null;
            readEndpointAddress = -1;
            writeEndpointAddress = -1;

            // Explicitly reset file descriptor
            fileDescriptor = -1;

            usbManager = null;
            usbDevice = null;
        }
    }
}

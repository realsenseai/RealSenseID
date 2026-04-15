// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.util;

import android.os.Environment;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.realsenseai.rsid.api.DeviceConfig;
import com.realsenseai.rsid.api.Status;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import timber.log.Timber;

public class FrameDumper {

  private static final String RAW10_EXTENSION = ".w10";
  private static final String SETTINGS_FILE_NAME = "settings.json";
  private static File baseDumpsDirectory;
  private File sessionSpecificDumpDirectory;

  /**
   * Default constructor for FrameDumper.
   * Creates the dumps directory in Downloads/Dumps for easy tablet access.
   * No initial session is created - call createNewSessionDirectory() to start a
   * session.
   */
  public FrameDumper() {
    Timber.d(
      "FrameDumper Default Constructor - Creating Downloads/Dumps directory");

    if (ensureBaseDumpsDirectoryExists()) {
      Timber.i("FrameDumper initialized with Downloads/Dumps directory: %s",
               baseDumpsDirectory.getAbsolutePath());
    }
    else {
      this.sessionSpecificDumpDirectory = null;
    }
  }

  /**
   * Utility method to get current device settings.
   * This can be called before creating a FrameDumper to get the current device
   * configuration.
   *
   * @return Current device configuration, or null if unable to retrieve
   */
  public static DeviceConfig getCurrentDeviceSettings() {
    try {
      var authenticator = SDKWrapper.INSTANCE.getAuthenticator();
      if (authenticator == null) {
        Timber.w(
          "Cannot get current device settings - authenticator not available");
        return null;
      }

      DeviceConfig config = new DeviceConfig();
      Status status = authenticator.QueryDeviceConfig(config);

      if (status == Status.Ok) {
        Timber.d(
          "Current settings: SecurityLevel=%s, DumpMode=%s, AlgoFlow=%s, CameraRotation=%s, MaxSpoofs=%d",
          config.getSecurity_level(), config.getDump_mode(),
          config.getAlgo_flow(), config.getCamera_rotation(),
          config.getMax_spoofs());
        return config;
      }
      else {
        Timber.w("Failed to query device settings, status: %s", status);
        return null;
      }
    }
    catch (Exception e) {
      Timber.e(e, "Exception while getting current device settings");
      return null;
    }
  }

  /**
   * Creates or ensures the base dumps directory exists.
   * This method can be called to recreate the dumps directory if it was
   * deleted.
   *
   * @return true if the dumps directory exists or was successfully created,
   * false otherwise
   */
  private boolean ensureBaseDumpsDirectoryExists() {
    // Create the standard dumps directory in Downloads
    File downloadsDir = Environment.getExternalStoragePublicDirectory(
      Environment.DIRECTORY_DOWNLOADS);
    if (downloadsDir == null) {
      Timber.e("Downloads directory not available.");
      baseDumpsDirectory = null;
      return false;
    }

    File dumpsDir = new File(downloadsDir, "Dumps");
    if (!dumpsDir.exists()) {
      boolean created = dumpsDir.mkdirs();
      if (!created) {
        Timber.e("Failed to create Dumps directory: %s",
                 dumpsDir.getAbsolutePath());
        baseDumpsDirectory = null;
        return false;
      }
      Timber.i("Created Dumps directory: %s", dumpsDir.getAbsolutePath());
    }

    baseDumpsDirectory = dumpsDir;
    return true;
  }

  /**
   * Converts DeviceConfig to JSON string using Gson
   *
   * @param deviceConfig The device configuration to convert
   * @return JSON string representation of the device configuration
   */
  private String deviceConfigToJson(DeviceConfig deviceConfig) {
    if (deviceConfig == null) {
      return "{\"note\": \"Device settings not available during dump session creation\"}";
    }

    // Create Gson instance with pretty printing
    Gson gson = new GsonBuilder().setPrettyPrinting().create();

    // Create a wrapper object with the values we want to serialize
    DeviceConfigWrapper wrapper = new DeviceConfigWrapper();
    wrapper.security_level = deviceConfig.getSecurity_level().toString();
    wrapper.dump_mode = deviceConfig.getDump_mode().toString();
    wrapper.algo_flow = deviceConfig.getAlgo_flow().toString();
    wrapper.camera_rotation = deviceConfig.getCamera_rotation().toString();
    wrapper.max_spoofs = deviceConfig.getMax_spoofs();
    wrapper.timestamp = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US)
      .format(new Date());
    wrapper.dump_session_created = System.currentTimeMillis();

    return gson.toJson(wrapper);
  }

  /**
   * Creates a settings.json file in the current sessionSpecificDumpDirectory
   * and writes the current device settings content to it.
   *
   * @param deviceConfig The device configuration to save. If null, saves a
   *                     placeholder.
   */
  private void createSettingsFileInSessionDir(DeviceConfig deviceConfig) {
    if (sessionSpecificDumpDirectory == null ||
        !sessionSpecificDumpDirectory.exists()) {
      Timber.e(
        "Cannot create settings.json, session directory is not available or does not exist.");
      return;
    }

    File settingsFile =
      new File(sessionSpecificDumpDirectory, SETTINGS_FILE_NAME);
    try (FileWriter writer = new FileWriter(settingsFile)) {
      if (deviceConfig != null) {
        String jsonContent = deviceConfigToJson(deviceConfig);
        writer.write(jsonContent);
        Timber.i("Device settings saved to settings.json in: %s",
                 settingsFile.getAbsolutePath());
      }
      else {
        // Fallback for backward compatibility
        writer.write(
          "{\"note\": \"Device settings not available during dump session creation\"}");
        Timber.w(
          "No device settings provided, saved placeholder to settings.json");
      }
    }
    catch (IOException e) {
      Timber.e(e, "Failed to create or write to settings.json in: %s",
               settingsFile.getAbsolutePath());
    }
  }

  /**
   * Checks if the dumper is ready to dump files (i.e., base dumps directory
   * exists). If the directory doesn't exist, attempts to recreate it.
   *
   * @return True if ready, false otherwise
   */
  public boolean isReady() {
    // First check if we have a valid base dumps directory
    if (baseDumpsDirectory != null && baseDumpsDirectory.exists()) {
      return true;
    }

    // If not, try to recreate it (handles case where user deleted the
    // directory)
    Timber.w("Dumps directory not ready, attempting to recreate it...");
    return ensureBaseDumpsDirectoryExists();
  }

  /**
   * Creates a new session directory for a new authentication or enrollment
   * operation. This should be called at the start of each authenticate/enroll
   * to create a fresh session.
   *
   * @param operationType "Authenticate" or "Enroll"
   * @param enrollId      The ID being enrolled (only used for Enroll operations,
   *                      should be null for Authenticate)
   * @return True if new session directory was created successfully, false
   * otherwise
   */
  public boolean createNewSessionDirectory(String operationType,
                                           String enrollId) {
    // Ensure base dumps directory exists before creating session directory
    if (!ensureBaseDumpsDirectoryExists()) {
      Timber.e(
        "Cannot create session directory - base dumps directory unavailable");
      return false;
    }

    String sessionTitle;
    if ("Enroll".equals(operationType) && enrollId != null &&
        !enrollId.trim().isEmpty()) {
      sessionTitle = "Enroll_" + enrollId.trim().replace(' ', '_');
    }
    else {
      sessionTitle = operationType != null ? operationType.replace(' ', '_')
                                           : "Authenticate";
    }

    String sessionDirName =
      sessionTitle + "_session_" + System.currentTimeMillis();
    this.sessionSpecificDumpDirectory =
      new File(baseDumpsDirectory, sessionDirName);

    if (!this.sessionSpecificDumpDirectory.exists()) {
      if (this.sessionSpecificDumpDirectory.mkdirs()) {
        Timber.i("New session dump directory created: %s",
                 this.sessionSpecificDumpDirectory.getAbsolutePath());

        // Get current device settings and create settings file
        DeviceConfig currentSettings = getCurrentDeviceSettings();
        createSettingsFileInSessionDir(currentSettings);
        return true;
      }
      else {
        Timber.e("Failed to create new session dump directory: %s",
                 sessionDirName);
        this.sessionSpecificDumpDirectory = null;
        return false;
      }
    }
    return true;
  }

  /**
   * Dumps a raw image to a .w10 file using pre-captured metadata.
   * This version doesn't access the Image object at all, avoiding lifecycle
   * issues.
   *
   * @param precopiedBuffer Pre-copied buffer containing image data
   * @param sessionType     The session type (e.g., "AUTH", "REG")
   * @param timestamp       The image timestamp
   * @param exposure        The exposure value
   * @param gain            The gain value
   * @param led             The LED value
   * @param sensor          The sensor name ("left" or "right")
   * @param status          The status value
   * @param imageSize       The image size in bytes
   * @return The full path of the dumped file, or null if the operation failed
   */
  public void dumpRawImageWithCapturedMetadata(
    byte[] precopiedBuffer, String sessionType, long timestamp, int exposure,
    long gain, int led, String sensor, long status, long imageSize) {
    if (sessionSpecificDumpDirectory == null) {
      Timber.e(
        "Cannot dump image - no session directory exists. Call createNewSessionDirectory first.");
      return;
    }

    if (!sessionSpecificDumpDirectory.exists() ||
        !sessionSpecificDumpDirectory.isDirectory()) {
      return;
    }

    if (precopiedBuffer == null) {
      return;
    }

    int actualImageSize = (int)imageSize;
    if (actualImageSize <= 0) {
      actualImageSize = precopiedBuffer.length;
    }

    if (actualImageSize > precopiedBuffer.length) {
      actualImageSize = precopiedBuffer.length;
    }

    // Generate filename with captured metadata
    String sessionPrefix = (sessionType != null && !sessionType.isEmpty())
                           ? sessionType
                           : "UNKNOWN";
    String ledStatus = (led > 0) ? "on" : "off";

    String filename = String.format(
      "%s_%d_exp_%d_gain_%d_led_%s_sensor_%s_status_%d%s", sessionPrefix,
      timestamp, exposure, gain, ledStatus, sensor, status, RAW10_EXTENSION);

    File fullPathFile = new File(sessionSpecificDumpDirectory, filename);

    try (FileOutputStream fs = new FileOutputStream(fullPathFile)) {
      fs.write(precopiedBuffer, 0, actualImageSize);
      Timber.d("DEBUG - Image dumped to: %s", fullPathFile.getAbsolutePath());
      return;
    }
    catch (Exception e) {
      return;
    }
  }

  /**
   * Simple wrapper class for serializing DeviceConfig data with Gson
   */
  private static class DeviceConfigWrapper {
    public String security_level;
    public String dump_mode;
    public String algo_flow;
    public String camera_rotation;
    public int max_spoofs;
    public String timestamp;
    public long dump_session_created;
  }
}

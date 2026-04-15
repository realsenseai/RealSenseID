// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample.ui.settings;

import static java.util.Objects.isNull;
import static java.util.Objects.requireNonNull;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.realsenseai.rsid.api.DeviceConfig;
import com.realsenseai.rsid.api.DeviceType;
import com.realsenseai.rsid.api.FaceAuthenticator;
import com.realsenseai.rsid.api.Status;
import com.realsenseai.rsid.sample.R;
import com.realsenseai.rsid.sample.databinding.FragmentSettingsBinding;
import com.realsenseai.rsid.sample.ui.shared.RealSenseIdSharedViewModel;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import com.realsenseai.rsid.sample.util.SnackbarHelper;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.stream.Collectors;
import timber.log.Timber;

public class SettingsFragment extends Fragment {

  private static final Map<DeviceConfig.DumpMode, Integer> DUMP_MODE_TO_ID = Map.of(
    DeviceConfig.DumpMode.None, R.id.dump_none,
    DeviceConfig.DumpMode.FullFrame, R.id.dump_full,
    DeviceConfig.DumpMode.CroppedFace, R.id.dump_face
  );

  private static final Map<DeviceConfig.SecurityLevel, Integer> SECURITY_LEVEL_TO_ID = Map.of(
    DeviceConfig.SecurityLevel.High, R.id.security_level_high,
    DeviceConfig.SecurityLevel.Medium, R.id.security_level_medium,
    DeviceConfig.SecurityLevel.Low, R.id.security_level_standard
  );

  private static final Map<DeviceConfig.AlgoFlow, Integer> ALGO_FLOW_TO_ID = Map.of(
    DeviceConfig.AlgoFlow.All, R.id.algo_all,
    DeviceConfig.AlgoFlow.FaceDetectionOnly, R.id.algo_detect,
    DeviceConfig.AlgoFlow.SpoofOnly, R.id.algo_spoof,
    DeviceConfig.AlgoFlow.RecognitionOnly, R.id.algo_recognition
  );
  private static final Map<DeviceConfig.CameraRotation, Integer> CAMERA_ROTATION_TO_ID = Map.of(
    DeviceConfig.CameraRotation.Rotation_0_Deg, R.id.camera_rotation_0,
    DeviceConfig.CameraRotation.Rotation_90_Deg, R.id.camera_rotation_90,
    DeviceConfig.CameraRotation.Rotation_180_Deg, R.id.camera_rotation_180,
    DeviceConfig.CameraRotation.Rotation_270_Deg, R.id.camera_rotation_270
  );

  private static final Map<DeviceConfig.FrontalFacePolicy, Integer> FRONTAL_FACE_POLICY_TO_ID = Map.of(
    DeviceConfig.FrontalFacePolicy.None, R.id.frontal_face_policy_none,
    DeviceConfig.FrontalFacePolicy.Moderate, R.id.frontal_face_policy_moderate,
    DeviceConfig.FrontalFacePolicy.Strict, R.id.frontal_face_policy_strict
  );

  private static final Map<DeviceConfig.FaceSelectionPolicy, Integer> FACE_SELECTION_POLICY_TO_ID = Map.of(
    DeviceConfig.FaceSelectionPolicy.All, R.id.face_selection_multi,
    DeviceConfig.FaceSelectionPolicy.Single, R.id.face_selection_single
  );

  private static final Map<DeviceConfig.PersonMotionMode, Integer> PERSON_MOTION_MODE_TO_ID = Map.of(
    DeviceConfig.PersonMotionMode.Static, R.id.person_motion_static,
    DeviceConfig.PersonMotionMode.Walkthrough, R.id.person_motion_walkthrough
  );

  private FragmentSettingsBinding binding;
  private ExecutorService executorService;

  @Nullable
  private FaceAuthenticator authenticator = null;

  @Override
  public View onCreateView(@NonNull LayoutInflater inflater,
                           ViewGroup container, Bundle savedInstanceState) {
    binding = FragmentSettingsBinding.inflate(inflater, container, false);
    View root = binding.getRoot();

    binding.dumpFace.setEnabled(false);
    binding.cameraRotation270.setEnabled(false);

    executorService = Executors.newSingleThreadExecutor();

    binding.dbMode.setEnabled(true);
    binding.dbMode.setOnClickListener(view -> {
      var sharedViewModel = new ViewModelProvider(requireActivity()).get(RealSenseIdSharedViewModel.class);
      sharedViewModel.setUseHostDb(binding.dbMode.isChecked());
    });


    binding.featuresLandmarks.addOnCheckedStateChangedListener((materialCheckBox, _ignored) -> {
      if (materialCheckBox.isChecked()) {
        binding.featuresRectangle.setChecked(true);
        binding.featuresRectangle.setEnabled(false);
      }
      else {
        binding.featuresRectangle.setEnabled(true);
      }
    });

    binding.buttonCommitDeviceSettings.setOnClickListener(this::saveHandler);
    binding.unlockButton.setOnClickListener(this::unlockHandler);

    return root;
  }

  @Override
  public void onResume() {
    super.onResume();
    try {
      loadDeviceSettings();
    }
    catch (Exception e) {
      Timber.e(e, "Error loading device settings");
      binding.buttonCommitDeviceSettings.setEnabled(false);
      binding.unlockButton.setEnabled(false);
      showErrorDialog("Error", "Failed to load device settings: " + e.getMessage());
    }
  }

  private int getAlgoPersonVisibility() {
    var sharedViewModel = new ViewModelProvider(requireActivity()).get(RealSenseIdSharedViewModel.class);
    var deviceType = sharedViewModel.getDeviceType().getValue();
    if (deviceType == DeviceType.F50x) {
      return View.VISIBLE;
    }
    else {
      return View.GONE;
    }
  }

  private void unlockHandler(View v) {
    setLoadingState(true);

    executorService.execute(() -> {
      synchronized (this) {

        try {

          if (authenticator == null) {
            Timber.e("Failed to get Authenticator handle");
            setLoadingState(false);
            showErrorDialog("Error", "Failed to get Authenticator handle");
            return;
          }
          authenticator.Connect(SDKWrapper.INSTANCE.getSerialConfig());
          Status status = authenticator.Unlock();

          requireActivity().runOnUiThread(() -> {
            setLoadingState(false);
            if (status == Status.Ok) {
              showSuccessMessage("Device Unlocked");
            }
            else {
              showErrorDialog("Error", "Failed to unlock device! Status: " + status.toString());
            }
          });
        }
        catch (Exception e) {
          Timber.e(e, "Error unlocking device");
          if (isAdded()) {
            requireActivity().runOnUiThread(() -> {
              setLoadingState(false);
              showErrorDialog("Error", "Failed to unlock device: " + e.getMessage());
            });
          }
        }
      }
    });
  }

  private void saveHandler(View v) {
    if (!validateInputs()) {
      return;
    }

    if (isNull(authenticator)) {
      showErrorDialog("Error", "Error updating device settings! Can't get Authenticator handle.");
      return;
    }
    authenticator.Connect(SDKWrapper.INSTANCE.getCachedOrNewSerialConfig());

    setLoadingState(true);
    DeviceConfig config = buildDeviceConfigFromUI();
    applyDeviceConfig(authenticator, config);
  }

  private boolean validateInputs() {
    try {
      var editText = binding.maxSpoofs.getEditText();
      if (editText == null) {
        showErrorDialog("Error", "Max spoofs field is not accessible");
        return false;
      }

      String maxSpoofsText = editText.getText().toString().trim();
      if (!maxSpoofsText.isEmpty()) {
        short value = Short.parseShort(maxSpoofsText);
        if (value < 0) {
          showErrorDialog("Invalid Input", "Max spoofs must be non-negative");
          return false;
        }
      }
      return true;
    }
    catch (NumberFormatException e) {
      showErrorDialog("Invalid Input", "Please enter a valid number for max spoofs");
      return false;
    }
  }

  private DeviceConfig buildDeviceConfigFromUI() {
    var config = new DeviceConfig();

    config.setDump_mode(getDumpModeFromSelection(binding.dumpModeRadioGroup.getCheckedRadioButtonId()));
    config.setSecurity_level(getSecurityLevelFromSelection(binding.securityLevelRadioGroup.getCheckedRadioButtonId()));
    config.setAlgo_flow(getAlgoFlowFromSelection(binding.algoFlowRadioGroup.getCheckedRadioButtonId()));
    config.setCamera_rotation(getCameraRotationFromSelection(binding.cameraRotationRadioGroup.getCheckedRadioButtonId()));
    config.setFrontal_face_policy(getFrontalFacePolicySelection(binding.frontalFacePolicyRadioGroup.getCheckedRadioButtonId()));
    config.setFace_selection_policy(getFaceSelectionPolicySelection(binding.faceSelectionRadioGroup.getCheckedRadioButtonId()));
    config.setPerson_motion_mode(getPersonMotionModeSelection(binding.personMotionRadioGroup.getCheckedRadioButtonId()));
    config.setRect_enable((short)(binding.featuresRectangle.isChecked() ? 1 : 0));
    config.setLandmarks_enable((short)(binding.featuresLandmarks.isChecked() ? 1 : 0));

    try {
      var editText = binding.maxSpoofs.getEditText();
      if (editText != null) {
        String maxSpoofsText = editText.getText().toString().trim();
        short maxSpoofs = maxSpoofsText.isEmpty() ? 0 : Short.parseShort(maxSpoofsText);
        config.setMax_spoofs(maxSpoofs);
      }
      else {
        config.setMax_spoofs((short)0);
      }
    }
    catch (NumberFormatException e) {
      Timber.w(e, "Failed to parse max spoofs, using default value 0");
      config.setMax_spoofs((short)0);
    }

    return config;
  }

  private void applyDeviceConfig(FaceAuthenticator authenticator, DeviceConfig config) {
    executorService.execute(() -> {
      synchronized (this) {
        try {
          Status status;
          status = authenticator.SetDeviceConfig(config);

          if (isAdded()) {
            Status finalStatus = status;
            requireActivity().runOnUiThread(() -> {
              setLoadingState(false);
              if (finalStatus == Status.Ok) {
                showSuccessMessage("Settings Applied");
                // Update shared ViewModel with the successfully applied settings
                var sharedViewModel = new ViewModelProvider(requireActivity())
                  .get(RealSenseIdSharedViewModel.class);
                sharedViewModel.setDumpMode(config.getDump_mode());
                sharedViewModel.setSecurityLevel(config.getSecurity_level());
                sharedViewModel.setOperationMode(config.getAlgo_flow());
                sharedViewModel.setCameraRotation(config.getCamera_rotation());

                Timber.d("Updated shared ViewModel with applied settings");
              }
              else {
                showErrorDialog("Error",
                                "Error updating device settings! Status: " +
                                finalStatus.toString());
              }
            });
          }
        }
        catch (Exception e) {
          Timber.e(e, "Error applying device config");
          if (isAdded()) {
            requireActivity().runOnUiThread(() -> {
              setLoadingState(false);
              showErrorDialog("Error", "Failed to apply settings: " + e.getMessage());
            });
          }
        }
      }
    });
  }

  private DeviceConfig.DumpMode getDumpModeFromSelection(int selectionId) {
    return Map.copyOf(DUMP_MODE_TO_ID.entrySet().stream()
                        .collect(Collectors.toMap(Map.Entry::getValue, Map.Entry::getKey)))
      .getOrDefault(selectionId, DeviceConfig.DumpMode.None);
  }

  private DeviceConfig.SecurityLevel getSecurityLevelFromSelection(int selectionId) {
    return Map.copyOf(SECURITY_LEVEL_TO_ID.entrySet().stream()
                        .collect(Collectors.toMap(Map.Entry::getValue, Map.Entry::getKey)))
      .getOrDefault(selectionId, DeviceConfig.SecurityLevel.Low);
  }

  private DeviceConfig.AlgoFlow getAlgoFlowFromSelection(int selectionId) {
    return Map.copyOf(ALGO_FLOW_TO_ID.entrySet().stream()
                        .collect(Collectors.toMap(Map.Entry::getValue, Map.Entry::getKey)))
      .getOrDefault(selectionId, DeviceConfig.AlgoFlow.All);
  }

  private DeviceConfig.CameraRotation getCameraRotationFromSelection(int selectionId) {
    return Map.copyOf(CAMERA_ROTATION_TO_ID.entrySet().stream()
                        .collect(Collectors.toMap(Map.Entry::getValue, Map.Entry::getKey)))
      .getOrDefault(selectionId, DeviceConfig.CameraRotation.Rotation_0_Deg);
  }

  private DeviceConfig.FrontalFacePolicy getFrontalFacePolicySelection(int selectionId) {
    return Map.copyOf(FRONTAL_FACE_POLICY_TO_ID.entrySet().stream()
                        .collect(Collectors.toMap(Map.Entry::getValue, Map.Entry::getKey)))
      .getOrDefault(selectionId, DeviceConfig.FrontalFacePolicy.None);
  }

  private DeviceConfig.FaceSelectionPolicy getFaceSelectionPolicySelection(int selectionId) {
    return Map.copyOf(FACE_SELECTION_POLICY_TO_ID.entrySet().stream()
                        .collect(Collectors.toMap(Map.Entry::getValue, Map.Entry::getKey)))
      .getOrDefault(selectionId, DeviceConfig.FaceSelectionPolicy.Single);
  }

  private DeviceConfig.PersonMotionMode getPersonMotionModeSelection(int selectionId) {
    return Map.copyOf(PERSON_MOTION_MODE_TO_ID.entrySet().stream()
                        .collect(Collectors.toMap(Map.Entry::getValue, Map.Entry::getKey)))
      .getOrDefault(selectionId, DeviceConfig.PersonMotionMode.Static);
  }

  private void loadDeviceSettings() {
    setLoadingState(true);

    executorService.execute(() -> {
      synchronized (this) {
        try {
          SDKWrapper.INSTANCE.closeConnection();
          authenticator = SDKWrapper.INSTANCE.getAuthenticator();

          if (isNull(authenticator)) {
            if (isAdded()) {
              requireActivity().runOnUiThread(() -> {
                setLoadingState(false);
                showErrorDialog("Error", "Error retrieving device settings! Can't get Authenticator handle.");
              });
            }
            return;
          }

          // Check if fragment is still attached before updating UI
          if (isAdded()) {
            try {
              DeviceConfig config = new DeviceConfig();
              requireNonNull(authenticator).QueryDeviceConfig(config);

              requireActivity().runOnUiThread(() -> {
                setLoadingState(false);
                updateUIFromConfig(requireNonNull(config));
              });
            }
            catch (Exception e) {
              showErrorDialog("Error", "Error retrieving device settings!");
              Timber.e(e, "Error loading device settings");
            }
          }
        }
        catch (Exception e) {
          Timber.e(e, "Error loading device settings");
          if (isAdded()) {
            requireActivity().runOnUiThread(() -> {
              setLoadingState(false);
              showErrorDialog("Error", "Failed to load device settings: " + e.getMessage());
            });
          }
        }
        finally {
          if (authenticator != null) {
            authenticator.Disconnect();
          }
        }
      }
    });
  }

  private void updateUIFromConfig(DeviceConfig config) {
    updateRadioGroup(binding.algoFlowRadioGroup, config.getAlgo_flow(), ALGO_FLOW_TO_ID);
    updateRadioGroup(binding.dumpModeRadioGroup, config.getDump_mode(), DUMP_MODE_TO_ID);
    updateRadioGroup(binding.securityLevelRadioGroup, config.getSecurity_level(), SECURITY_LEVEL_TO_ID);
    updateRadioGroup(binding.cameraRotationRadioGroup, config.getCamera_rotation(), CAMERA_ROTATION_TO_ID);
    updateMaxSpoofs(config);

    updateRadioGroup(binding.frontalFacePolicyRadioGroup, config.getFrontal_face_policy(), FRONTAL_FACE_POLICY_TO_ID);
    updateMaxSpoofs(config);
    updateRadioGroup(binding.faceSelectionRadioGroup, config.getFace_selection_policy(), FACE_SELECTION_POLICY_TO_ID);
    updateMaxSpoofs(config);
    updateRadioGroup(binding.personMotionRadioGroup, config.getPerson_motion_mode(), PERSON_MOTION_MODE_TO_ID);
    updateMaxSpoofs(config);
    binding.featuresRectangle.setChecked(config.getRect_enable() != 0);
    binding.featuresLandmarks.setChecked(config.getLandmarks_enable() != 0);
  }

  private void updateMaxSpoofs(DeviceConfig config) {
    if (binding == null) return;
    var maxSpoofs = config.getMax_spoofs();
    var editText = binding.maxSpoofs.getEditText();
    if (editText != null) {
      editText.setText(String.valueOf(maxSpoofs));
    }
  }

  private <T> void updateRadioGroup(android.widget.RadioGroup radioGroup, T value, Map<T, Integer> mapping) {
    if (binding == null || value == null) return;
    radioGroup.clearCheck();
    Integer radioId = mapping.get(value);
    if (radioId != null) {
      radioGroup.check(radioId);
    }
  }

  // UI Helper Methods
  private void showErrorDialog(String title, String message) {
    new MaterialAlertDialogBuilder(requireContext())
      .setTitle(title)
      .setMessage(message)
      .setPositiveButton("OK", (dialog, whichButton) -> dialog.dismiss())
      .show();
  }

  private void showSuccessMessage(String message) {
    SnackbarHelper.showSnackbarMessage(binding.settingsContentRoot, message, requireContext());
  }

  private void setLoadingState(boolean isLoading) {
    if (binding != null) {
      binding.buttonCommitDeviceSettings.setEnabled(!isLoading);
    }
  }

  @Override
  public void onDestroyView() {
    super.onDestroyView();

    if (authenticator != null) {
      authenticator.Disconnect();
      authenticator.delete();
    }

    if (executorService != null && !executorService.isShutdown()) {
      executorService.shutdown();
    }

    binding = null;
  }
}

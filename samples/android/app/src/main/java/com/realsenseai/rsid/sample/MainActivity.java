// Copyright (C) 2018-2025 RealSense, Inc.
// SPDX-License-Identifier: Apache-2.0

package com.realsenseai.rsid.sample;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.lifecycle.ViewModelProvider;
import androidx.navigation.NavController;
import androidx.navigation.Navigation;
import androidx.navigation.fragment.NavHostFragment;
import androidx.navigation.ui.AppBarConfiguration;
import androidx.navigation.ui.NavigationUI;
import com.google.android.material.bottomnavigation.BottomNavigationView;
import com.google.android.material.navigation.NavigationView;
import com.google.android.material.snackbar.Snackbar;
import com.permissionx.guolindev.PermissionX;
import com.realsenseai.rsid.api.DeviceType;
import com.realsenseai.rsid.sample.databinding.ActivityMainBinding;
import com.realsenseai.rsid.sample.databinding.NavHeaderMainBinding;
import com.realsenseai.rsid.sample.ui.firmware.FirmwareViewModel;
import com.realsenseai.rsid.sample.ui.shared.RealSenseIdSharedViewModel;
import com.realsenseai.rsid.sample.ui.users.UserListViewModel;
import com.realsenseai.rsid.sample.util.SDKWrapper;
import com.realsenseai.rsid.sample.util.SnackbarHelper;
import com.realsenseai.rsid.sample.util.UsbDevicesReceiver;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.opencv.android.OpenCVLoader;
import timber.log.Timber;

public class MainActivity extends AppCompatActivity
  implements ActivityCompat.OnRequestPermissionsResultCallback, UsbDevicesReceiver.UsbListener {
  private static final int PERMISSION_REQUEST_CAMERA = 1000;
  private static final int PERMISSION_REQUEST_STORAGE_READ = 1001;
  private static final int PERMISSION_REQUEST_STORAGE_WRITE = 1002;
  private static final String[] PERMISSIONS = {
    android.Manifest.permission.CAMERA,
    android.Manifest.permission.READ_EXTERNAL_STORAGE,
    android.Manifest.permission.WRITE_EXTERNAL_STORAGE
  };
  private final Handler mainHandler = new Handler(Looper.getMainLooper());
  private RealSenseIdSharedViewModel sharedViewModel;
  private UsbDevicesReceiver usbReceiver;
  private ActivityMainBinding binding;
  private AppBarConfiguration mAppBarConfiguration;

  @SuppressLint("SetTextI18n")
  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);

    // Initialize shared ViewModel
    sharedViewModel = new ViewModelProvider(this).get(RealSenseIdSharedViewModel.class);
    sharedViewModel.setUseHostDb(false);    // FIXME: This needs to be read from preferences.

    // Your existing code...
    binding = ActivityMainBinding.inflate(getLayoutInflater());
    setContentView(binding.getRoot());

    // Observe shared state for UI updates
    observeSharedState();

    try {
      if (binding.navView != null) {
        View headerView = binding.navView.getHeaderView(0);
        NavHeaderMainBinding navViewHeaderBinding = NavHeaderMainBinding.bind(headerView);
        navViewHeaderBinding.versionInfo.setText("ver: " + BuildConfig.GIT_SHA);
      }
    }
    catch (Exception e) {
      Timber.e(e, "Error in onCreate");
    }

    ExecutorService initExecutor = Executors.newSingleThreadExecutor();
    initExecutor.execute(() -> {
      boolean opencvLoaded = OpenCVLoader.initLocal();
      mainHandler.post(() -> {
        if (opencvLoaded) {
          Timber.i("OpenCV loaded successfully");
        }
        else {
          Timber.e("OpenCV initialization failed!");
          SnackbarHelper.showSnackbarMessage(binding.getRoot(), "OpenCV initialization failed!", this);
        }
      });
    });
    initExecutor.shutdown();


    setSupportActionBar(binding.appBarMain.toolbar);
    if (binding.appBarMain.fab != null) {
      binding.appBarMain.fab.setVisibility(View.GONE);    // TODO: Use?
      binding.appBarMain.fab.setOnClickListener(view -> Snackbar.make(view,
                                                                      "Replace with your own action", Snackbar.LENGTH_LONG)
        .setAction("Action", null).setAnchorView(R.id.fab).show());
    }

    PermissionX.init(this)
      .permissions(List.of(Manifest.permission.CAMERA,
                           Manifest.permission.WAKE_LOCK,
                           Manifest.permission.READ_EXTERNAL_STORAGE,
                           Manifest.permission.WRITE_EXTERNAL_STORAGE))
      .onExplainRequestReason(
        (scope, deniedList) ->
          scope.showRequestReasonDialog(deniedList,
                                        "The application needs to access the camera and " +
                                        "needs to be able to read/write user database files.",
                                        "OK", "Cancel")
      )
      .onForwardToSettings(
        (scope, deniedList) ->
          scope.showForwardToSettingsDialog(deniedList,
                                            "You need to allow necessary permissions in Settings manually",
                                            "OK", "Cancel")
      )
      .request(
        (allGranted, grantedList, deniedList) -> {
          if (allGranted) {
            SnackbarHelper.showSnackbarMessage(binding.getRoot(), "All permissions granted!", this);
          }
          else {
            SnackbarHelper.showSnackbarMessage(binding.getRoot(), "These permissions are denied: " + deniedList, this);
          }
        }
      );


    NavHostFragment navHostFragment = (NavHostFragment)getSupportFragmentManager().findFragmentById(R.id.nav_host_fragment_content_main);
    assert navHostFragment != null;
    NavController navController = navHostFragment.getNavController();

    // Listen for navigation changes to show/hide rotate and one-to-one switch
    navController.addOnDestinationChangedListener((controller, destination, arguments) -> {
      if (destination.getId() == R.id.nav_preview) {
        binding.appBarMain.buttonRotate.setVisibility(View.VISIBLE);
        binding.appBarMain.switchOneToOne.setVisibility(View.VISIBLE);
      }
      else {
        binding.appBarMain.buttonRotate.setVisibility(View.GONE);
        binding.appBarMain.switchOneToOne.setVisibility(View.GONE);
      }
    });

    // Setup one-to-one switch listener
    binding.appBarMain.switchOneToOne.setOnCheckedChangeListener((buttonView, isChecked) -> {
      sharedViewModel.setOneToOneMode(isChecked);
    });

    // Observe one-to-one mode changes to update switch state
    sharedViewModel.getOneToOneMode().observe(this, enabled -> {
      if (binding.appBarMain.switchOneToOne.isChecked() != enabled) {
        binding.appBarMain.switchOneToOne.setChecked(enabled);
      }
    });

    NavigationView navigationView = binding.navView;
    if (navigationView != null) {
      mAppBarConfiguration = new AppBarConfiguration.Builder(
        R.id.nav_preview, R.id.nav_users, R.id.nav_firmware, R.id.nav_settings)
        .setOpenableLayout(binding.drawerLayout)
        .build();
      NavigationUI.setupActionBarWithNavController(this, navController, mAppBarConfiguration);
      NavigationUI.setupWithNavController(navigationView, navController);
    }

    BottomNavigationView bottomNavigationView = binding.appBarMain.contentMain.bottomNavView;
    if (bottomNavigationView != null) {
      mAppBarConfiguration = new AppBarConfiguration.Builder(
        R.id.nav_preview, R.id.nav_users /*, R.id.nav_firmware */)
        .build();
      NavigationUI.setupActionBarWithNavController(this, navController, mAppBarConfiguration);
      NavigationUI.setupWithNavController(bottomNavigationView, navController);
    }


    setupAppBarIcon();

    mainHandler.postDelayed(() -> {
      if (usbReceiver != null && usbReceiver.isRealSenseIdDeviceAttached(this)) {
        var dev = usbReceiver.getFirstRealSenseIdDevice(this);
        if (dev != null) {
          onDeviceAttached(dev);
        }
        else {
          Timber.e("No devices attached");
        }
      }
    }, 500);
  }

  private void observeSharedState() {
    sharedViewModel.getUsbConnectionState().observe(this, isConnected -> {
      if (isConnected && sharedViewModel.isDeviceTypeSupported()) {
        binding.appBarMain.toolbarIcon.setImageResource(R.drawable.ic_f4xx_black_on_24dp);
      }
      else {
        binding.appBarMain.toolbarIcon.setImageResource(R.drawable.ic_f4xx_disc);
      }
    });

    sharedViewModel.getDeviceType().observe(this, deviceType -> {
      Timber.d("MainActivity: Device type changed to %s", deviceType);
    });

    sharedViewModel.getCompatibilityCheckRunning().observe(this, running -> {
      setNavigationEnabled(!running);
    });
  }

  private void setNavigationEnabled(boolean enabled) {
    NavigationView navigationView = binding.navView;
    if (navigationView != null) {
      navigationView.getMenu().setGroupEnabled(0, enabled);
    }
    BottomNavigationView bottomNav = binding.appBarMain.contentMain.bottomNavView;
    if (bottomNav != null) {
      for (int i = 0; i < bottomNav.getMenu().size(); i++) {
        bottomNav.getMenu().getItem(i).setEnabled(enabled);
      }
    }
  }

  private void setupAppBarIcon() {
    try {
      usbReceiver = new UsbDevicesReceiver(this);
      IntentFilter filter = new IntentFilter();
      filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
      filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
      this.registerReceiver(usbReceiver, filter);

      if (usbReceiver != null && usbReceiver.isRealSenseIdDeviceAttached(this)) {
        binding.appBarMain.toolbarIcon.setImageResource(R.drawable.ic_f4xx_black_on_24dp);
      }
      else {
        binding.appBarMain.toolbarIcon.setImageResource(R.drawable.ic_f4xx_disc);
      }
    }
    catch (Exception e) {
      Timber.e(e, "Error in setupAppBarIcon");
    }
  }

  @Override
  public boolean onCreateOptionsMenu(Menu menu) {
    boolean result = super.onCreateOptionsMenu(menu);
    // Using findViewById because NavigationView exists in different layout files
    // between w600dp and w1240dp
    NavigationView navView = findViewById(R.id.nav_view);
    if (navView == null) {
      // The navigation drawer already has the items including the items in the overflow menu
      // We only inflate the overflow menu if the navigation drawer isn't visible
      getMenuInflater().inflate(R.menu.overflow, menu);
    }
    return result;
  }

  @Override
  public boolean onOptionsItemSelected(@NonNull MenuItem item) {
    if (item.getItemId() == R.id.nav_settings) {
      NavController navController = Navigation.findNavController(this, R.id.nav_host_fragment_content_main);
      navController.navigate(R.id.nav_settings);
    }
    return super.onOptionsItemSelected(item);
  }

  @Override
  public boolean onSupportNavigateUp() {
    NavController navController = Navigation.findNavController(this, R.id.nav_host_fragment_content_main);
    return NavigationUI.navigateUp(navController, mAppBarConfiguration)
           || super.onSupportNavigateUp();
  }

  @Override
  public void onDeviceDetached(@Nullable UsbDevice usbDevice) {
    new ViewModelProvider(this).get(UserListViewModel.class).markDeviceUsersStale();
    new ViewModelProvider(this).get(FirmwareViewModel.class).markFirmwareInfoStale();
    sharedViewModel.handleDeviceDetached();
    binding.appBarMain.toolbarIcon.setImageResource(R.drawable.ic_f4xx_disc);
    SDKWrapper.INSTANCE.closeUVCConnection();
    SDKWrapper.INSTANCE.closeConnection();
  }

  @Override
  public void onDeviceAttached(@Nullable UsbDevice usbDevice) {
    new ViewModelProvider(this).get(UserListViewModel.class).markDeviceUsersStale();
    new ViewModelProvider(this).get(FirmwareViewModel.class).markFirmwareInfoStale();
    UsbDevicesReceiver usbReceiver = new UsbDevicesReceiver(null);
    DeviceType deviceType = usbReceiver.getAttachedDeviceType(this);
    sharedViewModel.handleDeviceAttached(deviceType);
    binding.appBarMain.toolbarIcon.setImageResource(
      R.drawable.ic_f4xx_black_on_24dp);

    // Request USB permission on the main thread (system dialog needs foreground activity),
    // then load device settings on a background thread once permission is granted.
    SDKWrapper.INSTANCE.ensurePermissionThenRun(this, () ->
      sharedViewModel.loadDeviceSettingsInBackground()
    );
  }
}
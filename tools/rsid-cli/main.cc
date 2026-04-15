// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

// Command line interface to RealSenseID device.

#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/Preview.h"
#include "RealSenseID/DeviceController.h"
#include "RealSenseID/DiscoverDevices.h"
#include "RealSenseID/Version.h"
#include "RealSenseID/Faceprints.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <direct.h> // for _mkdir
#else
#include <sys/stat.h> // for mkdir
#endif

#ifdef RSID_SECURE
#include "secure_mode_helper.h"
static RealSenseID::Examples::SignHelper s_signer;
#endif // RSID_SECURE

static std::map<std::string, RealSenseID::Faceprints> s_user_faceprint_db;
static RealSenseID::AuthenticateStatus s_last_auth_faceprint_status;
static RealSenseID::DeviceInfo s_device_info;

RealSenseID::SerialConfig get_serial_config()
{
    RealSenseID::SerialConfig config;
    config.port = s_device_info.serialPort;
    return config;
}

// Create a connected DeviceController, or print an error and return nullopt.
std::optional<RealSenseID::DeviceController> ConnectDeviceController()
{
    auto config = get_serial_config();
    RealSenseID::DeviceController controller(s_device_info.deviceType);
    auto status = controller.Connect(config);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed connecting to port " << config.port << " status:" << status << "\n";
        return std::nullopt;
    }
    return controller;
}

// Create a connected FaceAuthenticator, or exit on failure.
std::unique_ptr<RealSenseID::FaceAuthenticator> CreateAuthenticator()
{
    auto config = get_serial_config();
    if (s_device_info.deviceType == RealSenseID::DeviceType::Unknown)
    {
        std::cout << "Unknown device type for port " << config.port << "\n";
        std::exit(1);
    }
#ifdef RSID_SECURE
    auto authenticator = std::make_unique<RealSenseID::FaceAuthenticator>(&s_signer, s_device_info.deviceType);
#else
    auto authenticator = std::make_unique<RealSenseID::FaceAuthenticator>(s_device_info.deviceType);
#endif // RSID_SECURE
    auto status = authenticator->Connect(config);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed connecting to port " << config.port << " status:" << status << "\n";
        std::exit(1);
    }
    return authenticator;
}

// Run a continuous detection loop: start detection on a background thread,
// wait for Enter, then cancel and join.
template <typename DetectMethod, typename Callback>
void run_detection_loop(const char* label, DetectMethod method, Callback callback)
{
    auto authenticator = CreateAuthenticator();
    std::cout << "Running " << label << " (Press Enter to stop)...\n" << std::flush;
    std::thread t([&] {
        auto status = (authenticator.get()->*method)(callback, true);
        std::cout << label << " ended with status: " << status << std::endl;
    });
    std::cin.get();
    authenticator->Cancel();
    t.join();
    std::cout << "\n";
}

#ifdef RSID_PREVIEW

class PreviewRender : public RealSenseID::PreviewImageReadyCallback
{
public:
    void OnPreviewImageReady(const RealSenseID::Image& image) override
    {
        std::cout << "\rframe #" << image.number << ": " << image.width << "x" << image.height << " (" << image.size << " bytes)"
                  << std::endl;

        // uncomment to save as jpeg
        // std::ofstream ofs("frame_" + std::to_string(image.number) + ".jpg");
        // ofs.write(reinterpret_cast<const char*>(image.buffer), image.size);
    }
};

static std::unique_ptr<RealSenseID::Preview> s_preview;
static std::unique_ptr<PreviewRender> s_preview_callback;

#endif

class MyEnrollClbk : public RealSenseID::EnrollmentCallback
{
    using FacePose = RealSenseID::FacePose;

public:
    void OnResult(const RealSenseID::EnrollStatus status) override
    {
        std::cout << "  *** Result " << status << std::endl;
    }

    void OnProgress(const FacePose pose) override
    {
        std::cout << "  *** Detected Pose " << pose << std::endl;
        _poses_required.erase(pose);
        if (!_poses_required.empty())
        {
            auto next_pose = *_poses_required.begin();
            std::cout << "  *** Please Look To The " << next_pose << std::endl;
        }
    }

    void OnHint(const RealSenseID::EnrollStatus hint, float frameScore) override
    {
        std::cout << "  *** Hint " << hint << std::endl;
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        std::cout << "  *** OnFaceCroppedImage width:" << width << " height:" << height << " ts:" << ts << std::endl;
    }

private:
    std::set<RealSenseID::FacePose> _poses_required = {FacePose::Center, FacePose::Left, FacePose::Right};
};

void do_enroll(const char* user_id)
{
    auto authenticator = CreateAuthenticator();
    MyEnrollClbk enroll_clbk;
    auto status = authenticator->Enroll(enroll_clbk, user_id);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Status: " << status << "\n" << std::endl;
    }
}

class MyAuthClbk : public RealSenseID::AuthenticationCallback
{
public:
    void OnResult(const RealSenseID::AuthenticateStatus status, const char* user_id, short score) override
    {
        if (status == RealSenseID::AuthenticateStatus::Success)
        {
            std::cout << "******* Authenticate success.  user_id: " << user_id << " *******" << std::endl;
        }
        else
        {
            std::cout << "  *** Result: status: " << status << std::endl;
        }
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint, float frameScore) override
    {
        std::cout << "  *** Hint " << hint << std::endl;
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        std::cout << "  *** OnFaceCroppedImage width:" << width << " height:" << height << " ts:" << ts << std::endl;
    }

    void OnFaceDistances(const std::vector<double>& distances, const unsigned int ts) override
    {
        std::cout << "  *** OnFaceDistances " << distances.size() << " distances\n";
        for (size_t i = 0; i < distances.size(); i++)
        {
            std::cout << "Face " << (i + 1) << ": " << distances[i] << " cm ";
        }
        std::cout << std::endl;
    }
};

void do_authenticate()
{
    auto authenticator = CreateAuthenticator();
    MyAuthClbk auth_clbk;
    auto status = authenticator->Authenticate(auth_clbk);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Status: " << status << "\n" << std::endl;
    }
}

void do_authenticate_loop()
{
    auto authenticator = CreateAuthenticator();
    std::cout << "Running authenticate loop (Press Enter to stop)...\n" << std::flush;
    std::thread t([&] {
        MyAuthClbk auth_clbk;
        auto status = authenticator->AuthenticateLoop(auth_clbk);
        std::cout << "Authenticate loop ended with status: " << status << std::endl;
    });
    std::cin.get();
    authenticator->Cancel();
    t.join();
    std::cout << "\n";
}

void do_detect_persons()
{
    auto person_callback = [](const std::vector<RealSenseID::PersonRect>& persons, unsigned int ts,
                              RealSenseID::AuthenticateStatus status) {
        std::cout << "  *** Detected " << persons.size() << " person(s) at ts=" << ts << " status=" << status << std::endl;
        for (size_t i = 0; i < persons.size(); i++)
        {
            const auto& p = persons[i];
            std::cout << "      Person[" << i << "]: x=" << p.x << " y=" << p.y << " w=" << p.w << " h=" << p.h << " id=" << p.id
                      << " distance=" << p.distance << " body_part=" << static_cast<int>(p.body_part) << std::endl;
        }
        return true;
    };

    run_detection_loop("person detection", &RealSenseID::FaceAuthenticator::DetectPersons, person_callback);
}

void do_detect_poses()
{
    auto pose_callback = [](const std::vector<RealSenseID::PersonPose>& poses, unsigned int ts, RealSenseID::AuthenticateStatus status) {
        std::cout << "  *** Detected " << poses.size() << " pose(s) at ts=" << ts << " status=" << status << std::endl;
        for (size_t i = 0; i < poses.size(); i++)
        {
            const auto& p = poses[i];
            std::cout << "      Pose[" << i << "]: bbox(x=" << p.x << " y=" << p.y << " w=" << p.w << " h=" << p.h << ")" << std::endl;
            std::cout << "      Keypoints:" << std::endl;
            for (int j = 0; j < NUM_POSE_LANDMARKS; j++)
            {
                std::cout << "        [" << j << "]: (" << p.lm_x[j] << ", " << p.lm_y[j] << ") score=" << p.lm_score[j] << std::endl;
            }
        }
        return true;
    };

    run_detection_loop("pose detection", &RealSenseID::FaceAuthenticator::DetectPoses, pose_callback);
}

void do_detect_body_parts()
{
    auto body_part_callback = [](const std::vector<RealSenseID::PersonRect>& body_parts, unsigned int ts,
                                 RealSenseID::AuthenticateStatus status) {
        std::cout << "  *** Detected " << body_parts.size() << " body part(s) at ts=" << ts << " status=" << status << std::endl;
        const char* body_part_names[] = {"Person", "Foot", "Arm", "Leg", "Hand", "Torso"};
        for (size_t i = 0; i < body_parts.size(); i++)
        {
            const auto& bp = body_parts[i];
            int part_idx = static_cast<int>(bp.body_part);
            const char* part_name =
                (part_idx >= 0 && part_idx < static_cast<int>(std::size(body_part_names))) ? body_part_names[part_idx] : "Unknown";
            std::cout << "      BodyPart[" << i << "]: x=" << bp.x << " y=" << bp.y << " w=" << bp.w << " h=" << bp.h << " id=" << bp.id
                      << " distance=" << bp.distance << " type=" << part_name << std::endl;
        }
        return true;
    };

    run_detection_loop("body part detection", &RealSenseID::FaceAuthenticator::DetectBodyParts, body_part_callback);
}

void do_decode_barcodes()
{
    auto barcode_callback = [](const std::vector<std::string>& barcodes, unsigned int ts, RealSenseID::AuthenticateStatus status) {
        std::cout << "  *** Decoded " << barcodes.size() << " barcode(s) at ts=" << ts << " status=" << status << std::endl;
        for (size_t i = 0; i < barcodes.size(); i++)
        {
            const auto& barcode = barcodes[i];
            std::cout << "      Barcode[" << i << "]: \"" << barcode << "\" (length=" << barcode.length() << ")" << std::endl;
        }
        return true;
    };

    run_detection_loop("barcode decoding", &RealSenseID::FaceAuthenticator::DecodeBarcodes, barcode_callback);
}

void remove_users()
{
    auto authenticator = CreateAuthenticator();
    auto auth_status = authenticator->RemoveAll();
    std::cout << "Final status:" << auth_status << "\n" << std::endl;
}

#ifdef RSID_SECURE
void pair_device()
{
    auto authenticator = CreateAuthenticator();
    char* host_pubkey = (char*)s_signer.GetHostPubKey();
    char host_pubkey_signature[64] = {0};
    char device_pubkey[64] = {0};
    auto pair_status = authenticator->Pair(host_pubkey, host_pubkey_signature, device_pubkey);
    if (pair_status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed pairing with device\n";
        return;
    }
    s_signer.UpdateDevicePubKey((unsigned char*)device_pubkey);
    std::cout << "Final status:" << pair_status << "\n" << std::endl;
}

void unpair_device()
{
    auto authenticator = CreateAuthenticator();
    auto unpair_status = authenticator->Unpair();
    if (unpair_status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed to unpair with device\n";
        return;
    }
    std::cout << "Final status:" << unpair_status << "\n" << std::endl;
}
#endif // RSID_SECURE

std::optional<RealSenseID::DeviceConfig> query_device_config()
{
    auto authenticator = CreateAuthenticator();
    RealSenseID::DeviceConfig config;
    auto status = authenticator->QueryDeviceConfig(config);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed to query device config: " << status << "\n" << std::endl;
        return std::nullopt;
    }
    return config;
}

int rotation_to_degrees(RealSenseID::DeviceConfig::CameraRotation rot)
{
    using DC = RealSenseID::DeviceConfig;
    switch (rot)
    {
    case DC::CameraRotation::Rotation_90_Deg:
        return 90;
    case DC::CameraRotation::Rotation_180_Deg:
        return 180;
    case DC::CameraRotation::Rotation_270_Deg:
        return 270;
    default:
        return 0;
    }
}

void print_config(const RealSenseID::DeviceConfig& config, bool numbered)
{
    using DC = RealSenseID::DeviceConfig;
    auto motion = config.person_motion_mode == DC::PersonMotionMode::Walkthrough ? "Walkthrough" : "Static";
    const char* fmt_s = numbered ? "    %-30s%s\n" : "  %-24s%s\n";
    const char* fmt_d = numbered ? "    %-30s%d\n" : "  %-24s%d\n";

    printf(fmt_s, numbered ? "1.  Security Level" : "Security", Description(config.security_level));
    printf(fmt_s, numbered ? "2.  Algo Flow" : "Algo Flow", Description(config.algo_flow));
    printf(fmt_s, numbered ? "3.  Face Selection" : "Face Selection", Description(config.face_selection_policy));
    printf(fmt_s, numbered ? "4.  Dump Mode" : "Dump Mode", Description(config.dump_mode));
    printf(fmt_s, numbered ? "5.  Frontal Face Policy" : "Frontal Face Policy", Description(config.frontal_face_policy));
    printf(fmt_s, numbered ? "6.  Person Motion Mode" : "Person Motion Mode", motion);
    printf(fmt_d, numbered ? "7.  Rotation" : "Rotation", rotation_to_degrees(config.camera_rotation));
    printf(fmt_d, numbered ? "8.  Max Spoof Attempts" : "Max Spoof Attempts", static_cast<int>(config.max_spoofs));
    printf(fmt_d, numbered ? "9.  Matching Threshold" : "Matching Threshold", config.match_thresh);
    printf(fmt_d, numbered ? "10. GPIO Auth Toggling" : "GPIO Auth Toggling", config.gpio_auth_toggling);
    printf(fmt_s, numbered ? "11. Distance Limit" : "Distance Limit", Description(config.distance_limit));
    printf(fmt_d, numbered ? "12. Distance Enabled" : "Distance Enabled", static_cast<int>(config.distance_enabled));
    printf(fmt_d, numbered ? "13. Exposure Time (us)" : "Exposure Time (us)", config.manual_exposure_time_us);
    printf(fmt_d, numbered ? "14. Manual Gain" : "Manual Gain", config.manual_gain);
    printf(fmt_d, numbered ? "15. Rectangle Enabled" : "Rectangle Enabled", static_cast<int>(config.rect_enable));
    printf(fmt_d, numbered ? "16. Landmarks Enabled" : "Landmarks Enabled", static_cast<int>(config.landmarks_enable));
    printf(fmt_d, numbered ? "17. Num ROIs" : "Num ROIs", static_cast<int>(config.num_rois));
    auto roi_fmt = numbered ? "    %-30s[%d,%d] %dx%d\n" : "  %-24s[%d,%d] %dx%d\n";
    for (unsigned char ri = 0; ri < config.num_rois && ri < RealSenseID::DeviceConfig::MAX_ROIS; ri++)
    {
        char roi_label[32];
        if (numbered)
            snprintf(roi_label, sizeof(roi_label), "%d. ROI[%d]", 18 + ri, ri);
        else
            snprintf(roi_label, sizeof(roi_label), "ROI[%d]", ri);
        printf(roi_fmt, roi_label, config.detection_rois[ri].x, config.detection_rois[ri].y, config.detection_rois[ri].width,
               config.detection_rois[ri].height);
    }
}

void show_device_config()
{
    auto config = query_device_config();
    if (!config)
        return;
    printf("\nDevice Config:\n");
    printf("----------------------------------------------\n");
    print_config(*config, false);
    printf("----------------------------------------------\n\n");
}

// Read an integer from stdin in [min_val, max_val]. Retries until valid.
// If default_val is provided, shows it in [brackets] and returns it on empty input.
int get_int_from_user(const char* prompt, int min_val, int max_val, std::optional<int> default_val = std::nullopt)
{
    std::string input;
    while (true)
    {
        if (default_val.has_value())
            printf("%s[%d] ", prompt, *default_val);
        else
            printf("%s", prompt);
        std::getline(std::cin, input);
        if (input.empty() && default_val.has_value())
            return *default_val;
        std::istringstream iss(input);
        int value = 0;
        if (iss >> value && iss.eof() && value >= min_val && value <= max_val)
            return value;
        printf("Invalid input. Enter a number between %d and %d.\n", min_val, max_val);
    }
}

template <typename T>
void set_field(T& field, const char* prompt, int min_val, int max_val)
{
    field = static_cast<T>(get_int_from_user(prompt, min_val, max_val, static_cast<int>(field)));
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Read a string from stdin. Accepts a number (1-based index) or the option string (case-insensitive).
// Retries until valid. If default_val is non-empty, shows it in [brackets] and returns it on empty input.
std::string get_string_from_user(const char* prompt, const std::vector<std::string>& valid_options, const std::string& default_val = "")
{
    std::string input;
    while (true)
    {
        printf("%s(", prompt);
        for (size_t i = 0; i < valid_options.size(); i++)
            printf("%s%zu:%s", i ? " / " : "", i + 1, valid_options[i].c_str());
        if (!default_val.empty())
            printf(") [%s] ", default_val.c_str());
        else
            printf(") ");
        std::getline(std::cin, input);
        if (input.empty() && !default_val.empty())
            return default_val;
        std::istringstream iss(input);
        int idx = 0;
        if (iss >> idx && iss.eof() && idx >= 1 && idx <= static_cast<int>(valid_options.size()))
            return valid_options[idx - 1];
        auto lower_input = to_lower(input);
        if (std::find(valid_options.begin(), valid_options.end(), lower_input) != valid_options.end())
            return lower_input;
        printf("Invalid input.\n");
    }
}

void get_users()
{
    auto authenticator = CreateAuthenticator();

    unsigned int number_of_users = 0;
    auto status = authenticator->QueryNumberOfUsers(number_of_users);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Status: " << status << "\n" << std::endl;
        return;
    }

    if (number_of_users == 0)
    {
        std::cout << "No users found\n\n";
        return;
    }

    // allocate needed array of user ids using vectors for automatic cleanup
    std::vector<std::vector<char>> user_id_storage(number_of_users, std::vector<char>(RealSenseID::MAX_USERID_LENGTH));
    std::vector<char*> user_ids(number_of_users);
    for (unsigned int i = 0; i < number_of_users; i++)
    {
        user_ids[i] = user_id_storage[i].data();
    }

    unsigned int nusers_in_out = number_of_users;
    status = authenticator->QueryUserIds(user_ids.data(), nusers_in_out);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Status: " << status << "\n" << std::endl;
        return;
    }

    std::cout << "\n" << nusers_in_out << " Users:\n==========\n";
    for (unsigned int i = 0; i < (std::min)(nusers_in_out, number_of_users); i++)
    {
        std::cout << (i + 1) << ".  " << user_ids[i] << "\n";
    }

    std::cout << "\n";
}

void standby()
{
    auto authenticator = CreateAuthenticator();
    auto status = authenticator->Standby();
    std::cout << "Status: " << status << "\n" << std::endl;
}

void hibernate()
{
    auto authenticator = CreateAuthenticator();
    auto status = authenticator->Hibernate();
    std::cout << "Status: " << status << "\n" << std::endl;
}

void device_info()
{
    auto controller = ConnectDeviceController();
    if (!controller)
        return;

    std::string firmware_version;
    auto status = controller->QueryFirmwareVersion(firmware_version);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed getting firmware version!\n";
    }

    std::string serial_number;
    status = controller->QuerySerialNumber(serial_number);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed getting serial number!\n";
    }

    controller->Disconnect();

    std::cout << "\n";
    std::cout << "Additional information:\n";
    std::cout << " * Device: " << s_device_info.deviceType << "\n";
    std::cout << " * S/N: " << serial_number << "\n";
    std::cout << " * Firmware: " << firmware_version << "\n";
    std::cout << " * Host: " << RealSenseID::Version() << "\n";
    std::cout << "\n";
}


// ping X iterations and display roundtrip times
void ping_device(int iters)
{
    if (iters < 1)
    {
        return;
    }

    auto controller = ConnectDeviceController();
    if (!controller)
        return;

    using clock = std::chrono::steady_clock;
    for (int i = 0; i < iters; i++)
    {
        auto start_time = clock::now();
        auto status = controller->Ping();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start_time).count();
        printf("Ping #%04d %s. Roundtrip %03zu millis\n\n", (i + 1), RealSenseID::Description(status), elapsed_ms);
        if (status != RealSenseID::Status::Ok)
        {
            std::cout << "Ping error\n" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds {5});
    }
}

void unlock()
{
    auto authenticator = CreateAuthenticator();
    auto status = authenticator->Unlock();
    std::cout << "Status: " << status << "\n";
    if (status == RealSenseID::Status::Ok)
    {
        std::cout << "Device unlocked\n\n";
    }
}

// Fetch logs from the device and save to a file.
void query_log()
{
    auto controller = ConnectDeviceController();
    if (!controller)
        return;

    std::string log;
    std::cout << "Fetching device log...\n" << std::flush;
    auto status = controller->FetchLog(log);

    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed getting logs!\n";
        return;
    }

    controller->Disconnect();

    // create dumps dir if not exist and save the log in it
    std::string dumps_dir = "dumps";
    std::string logfile = dumps_dir + "/f500.log";
#ifdef _WIN32
    int rv = _mkdir(dumps_dir.c_str());
#else
    int rv = mkdir(dumps_dir.c_str(), 0777);
#endif // _WIN32
    if (rv == -1 && errno != EEXIST)
    {
        std::string msg = "Error creating directory " + dumps_dir;
        std::perror(msg.c_str());
        return;
    }
    std::ofstream ofs(logfile);
    ofs << log;
    if (ofs)
    {
        std::cout << "\n*** Saved to " << logfile << " (" << log.size() << " bytes) ***\n\n";
    }
    else
    {
        std::perror(logfile.c_str());
    }
}

void color_gains()
{
    auto controller = ConnectDeviceController();
    if (!controller)
        return;

    int red = -1, blue = -1;
    std::cout << "GetColorGains..\n";
    auto status = controller->GetColorGains(red, blue);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed getting color gains!\n";
        return;
    }

    std::cout << "Current Red-Blue: " << red << " " << blue;

    int input_red = -1, input_blue = -1;
    while (true)
    {
        std::string input;
        input_red = input_blue = -1;
        std::cout << "\nSet New Red-Blue (e.g. \"64 70\"): " << std::flush;
        std::getline(std::cin, input);
        if (input.empty())
            break;
        std::istringstream iss(input);
        if (iss >> input_red && iss >> input_blue && iss.eof())
            break;
    }
    status = controller->SetColorGains(input_red, input_blue);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed setting color gains!\n";
        return;
    }
    std::cout << "SetColorGains Success\n";
    std::cout << "GetColorGains..\n";
    status = controller->GetColorGains(red, blue);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed getting color gains!\n";
        return;
    }

    std::cout << "Got values: " << red << " " << blue << "\n";
}

void get_temperature()
{
    auto controller = ConnectDeviceController();
    if (!controller)
        return;

    float soc = 0;
    float board = 0;
    auto status = controller->GetTemperature(soc, board);
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Failed getting temperature!\n";
        return;
    }
    std::cout << "Temperature:\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "* SoC:   " << soc << " Celsius\n";
    std::cout << "* Board: " << board << " Celsius\n\n";
}

void reboot_device()
{
    auto controller = ConnectDeviceController();
    if (!controller)
        return;

    auto status = controller->Reboot();
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "Reboot failed!\n";
    }

    std::cout << "Rebooting..\n";
    // connect again to show device info after reboot
    std::this_thread::sleep_for(std::chrono::seconds(3));
    device_info();
}

void save_debug()
{
    auto authenticator = CreateAuthenticator();
    std::cout << "Saving debug data to device storage. Please wait..\n";
    auto status = authenticator->DumpAndMount();
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "SaveDebugData failed\n";
        return;
    }
    std::cout << "Please check the mounted folder (DUMPS)\n";
    std::exit(0);
}

void debug_mode()
{
    using RealSenseID::DeviceConfig;
    auto config = query_device_config();
    if (!config)
        return;

    auto choice = get_string_from_user("Enable debug dumps? ", {"y", "n"});
    config->dump_mode = (choice == "y") ? DeviceConfig::DumpMode::Debug : DeviceConfig::DumpMode::None;

    auto authenticator = CreateAuthenticator();
    auto status = authenticator->SetDeviceConfig(*config);
    std::cout << "Status: " << status << "\n" << std::endl;
}

void mount_debug()
{
    auto authenticator = CreateAuthenticator();
    std::cout << "Mounting..\n";
    auto status = authenticator->MountDebug();
    if (status != RealSenseID::Status::Ok)
    {
        std::cout << "MountDebug failed\n";
        return;
    }
    std::cout << "Please check the mounted folder (DUMPS)\n";
    std::exit(0);
}

class MyEnrollServerClbk : public RealSenseID::EnrollFaceprintsExtractionCallback
{
    std::string _user_id;

public:
    explicit MyEnrollServerClbk(const char* user_id) : _user_id(user_id)
    {
    }

    void OnResult(const RealSenseID::EnrollStatus status, const RealSenseID::ExtractedFaceprints* faceprints) override
    {
        std::cout << "on_result: status: " << status << "\n";

        if (status != RealSenseID::EnrollStatus::Success)
            return;

        // Copy extracted features into both the enrollment and adaptive descriptors.
        auto& entry = s_user_faceprint_db[_user_id];
        entry.data.version = faceprints->data.version;
        entry.data.flags = faceprints->data.flags;
        entry.data.featuresType = faceprints->data.featuresType;

        static_assert(sizeof(entry.data.adaptiveDescriptorWithoutMask) == sizeof(faceprints->data.featuresVector),
                      "adaptive faceprints vector (without mask) sizes does not match");
        ::memcpy(entry.data.adaptiveDescriptorWithoutMask, faceprints->data.featuresVector, sizeof(faceprints->data.featuresVector));

        static_assert(sizeof(entry.data.enrollmentDescriptor) == sizeof(faceprints->data.featuresVector),
                      "enrollment faceprints vector sizes does not match");
        ::memcpy(entry.data.enrollmentDescriptor, faceprints->data.featuresVector, sizeof(faceprints->data.featuresVector));
    }

    void OnProgress(const RealSenseID::FacePose pose) override
    {
        std::cout << "on_progress: pose: " << pose << "\n";
    }

    void OnHint(const RealSenseID::EnrollStatus hint, float frameScore) override
    {
        std::cout << "on_hint: hint: " << hint << " frame score: " << frameScore << "\n";
    }
};

void enroll_faceprints(const char* user_id)
{
    auto authenticator = CreateAuthenticator();
    MyEnrollServerClbk enroll_clbk {user_id};
    auto status = authenticator->ExtractFaceprintsForEnroll(enroll_clbk);
    std::cout << "Status: " << status << "\n" << std::endl;
}

class FaceprintsAuthClbk : public RealSenseID::AuthFaceprintsExtractionCallback
{
    RealSenseID::FaceAuthenticator* _authenticator;

public:
    explicit FaceprintsAuthClbk(RealSenseID::FaceAuthenticator* authenticator) : _authenticator(authenticator)
    {
    }

    void OnResult(const RealSenseID::AuthenticateStatus status, const RealSenseID::ExtractedFaceprints* faceprints) override
    {
        std::cout << "on_result: status: " << status << "\n";

        if (status != RealSenseID::AuthenticateStatus::Success)
        {
            std::cout << "ExtractFaceprints failed with status " << s_last_auth_faceprint_status << "\n\n";
            return;
        }

        RealSenseID::MatchElement scanned_faceprint;
        scanned_faceprint.data.version = faceprints->data.version;
        scanned_faceprint.data.featuresType = faceprints->data.featuresType;
        scanned_faceprint.data.flags = RealSenseID::FaOperationFlagsEnum::OpFlagAuthWithoutMask;

        static_assert(sizeof(scanned_faceprint.data.featuresVector) == sizeof(faceprints->data.featuresVector),
                      "faceprints vector sizes do not match");
        ::memcpy(scanned_faceprint.data.featuresVector, faceprints->data.featuresVector, sizeof(faceprints->data.featuresVector));

        std::cout << "\nSearching " << s_user_faceprint_db.size() << " faceprints\n";

        int best_score = -1;
        std::string winning_id;
        RealSenseID::MatchResultHost winning_match;
        RealSenseID::Faceprints winning_updated;

        auto matcher_confidence_level = RealSenseID::ThresholdsConfidenceEnum::ThresholdsConfidenceLevel_High;

        for (auto& [user_id, db_faceprint] : s_user_faceprint_db)
        {
            RealSenseID::Faceprints existing = db_faceprint;
            RealSenseID::Faceprints updated = existing;

            auto match = _authenticator->MatchFaceprints(scanned_faceprint, existing, updated, matcher_confidence_level);

            if (match.success && static_cast<int>(match.score) > best_score)
            {
                best_score = static_cast<int>(match.score);
                winning_match = match;
                winning_id = user_id;
                winning_updated = updated;
            }
        }

        if (!winning_id.empty())
        {
            std::cout << "\n******* Match success. user_id: " << winning_id << " *******\n\n";
            if (winning_match.should_update)
            {
                s_user_faceprint_db[winning_id] = winning_updated;
                std::cout << "DB adaptive update applied to user = " << winning_id << ".\n";
            }
        }
        else
        {
            std::cout << "\n******* Forbidden (no faceprint matched) *******\n\n";
        }
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint, float frameScore) override
    {
        std::cout << "on_hint: hint: " << hint << "\n";
    }
};

void authenticate_faceprints()
{
    auto authenticator = CreateAuthenticator();
    FaceprintsAuthClbk clbk(authenticator.get());
    s_last_auth_faceprint_status = RealSenseID::AuthenticateStatus::CameraStarted;
    auto status = authenticator->ExtractFaceprintsForAuth(clbk);
    if (status != RealSenseID::Status::Ok)
        std::cout << "Status: " << status << "\n" << std::endl;
}

void print_menu_opt(const char* line)
{
    std::cout << "  " << line << "\n";
}

void print_menu()
{
    std::cout << "\nChoose an option ('?' for menu, 'q' to quit):\n";
    print_menu_opt("'e' Enroll");
    print_menu_opt("'a' Authenticate");
    print_menu_opt("'t' Authenticate Loop (Press Enter to stop)");
    print_menu_opt("'d' Delete all users");
#ifdef RSID_SECURE
    print_menu_opt("'p' Pair device (enable secure comms)");
    print_menu_opt("'i' Unpair device (disable secure comms)");
#endif // RSID_SECURE
#ifdef RSID_PREVIEW
    print_menu_opt("'c' Capture frames");
#endif // RSID_PREVIEW
    print_menu_opt("'s' Set auth settings");
    print_menu_opt("'g' Show auth settings");
    print_menu_opt("'u' List users");
    print_menu_opt("'b' Standby");
    print_menu_opt("'h' Hibernate");
    print_menu_opt("'L' Unlock");
    print_menu_opt("\nDetection (Press Enter to stop):");
    print_menu_opt("'0' Persons");
    print_menu_opt("'1' Poses");
    print_menu_opt("'2' Body parts");
    print_menu_opt("'3' Barcodes");

    print_menu_opt("\nHost mode:");
    print_menu_opt("'E' Enroll with faceprints");
    print_menu_opt("'A' Authenticate with faceprints");
    print_menu_opt("'U' List enrolled users");
    print_menu_opt("'D' Delete all users");

    print_menu_opt("\nDebug:");
    print_menu_opt("'B' Toggle debug dumps");
    print_menu_opt("'F' Save debug dumps to flash");
    print_menu_opt("'M' Mount debug dumps");
    print_menu_opt("'o' Fetch device logs");
    print_menu_opt("'v' View device info");
    print_menu_opt("'f' Read temperature");
    print_menu_opt("'w' Set/get color gains");
    print_menu_opt("'x' Ping device");
    print_menu_opt("'R' Reboot device");
}

// Prompt for a non-empty user id string from stdin.
std::string prompt_user_id()
{
    std::string user_id;
    do
    {
        std::cout << "User id to enroll: ";
        std::getline(std::cin, user_id);
    } while (user_id.empty());
    return user_id;
}

void print_config_menu(const RealSenseID::DeviceConfig& config)
{
    printf("\nConfigure Device:\n");
    print_config(config, true);
    printf("\n    (d)efaults / (q)uit\n\n");
}

void configure_device()
{
    using DC = RealSenseID::DeviceConfig;

    auto config = query_device_config();
    if (!config)
        return;

    std::string input;
    while (true)
    {
        print_config_menu(*config);
        std::cout << "Select setting to change: ";
        std::getline(std::cin, input);

        if (input == "q")
            return;

        if (input == "d")
        {
            *config = DC {};
            auto authenticator = CreateAuthenticator();
            auto status = authenticator->SetDeviceConfig(*config);
            std::cout << "Defaults applied. Status: " << status << "\n";
            continue;
        }

        std::istringstream iss(input);
        int choice = 0;
        if (!(iss >> choice) || !iss.eof() || choice < 1 || choice > 22)
        {
            std::cout << "Invalid input. Enter 1-22, 'd' for defaults, or 'q' to quit.\n";
            continue;
        }

        switch (choice)
        {
        case 1: {
            auto val = get_string_from_user("Security level: ", {"high", "medium", "low"},
                                            to_lower(RealSenseID::Description(config->security_level)));
            if (val == "medium")
                config->security_level = DC::SecurityLevel::Medium;
            else if (val == "low")
                config->security_level = DC::SecurityLevel::Low;
            else
                config->security_level = DC::SecurityLevel::High;
            break;
        }
        case 2: {
            auto val = get_string_from_user("Algo flow: ", {"all", "recognitiononly", "facedetectiononly", "spoofonly"},
                                            to_lower(RealSenseID::Description(config->algo_flow)));
            if (val == "recognitiononly")
                config->algo_flow = DC::AlgoFlow::RecognitionOnly;
            else if (val == "facedetectiononly")
                config->algo_flow = DC::AlgoFlow::FaceDetectionOnly;
            else if (val == "spoofonly")
                config->algo_flow = DC::AlgoFlow::SpoofOnly;
            else
                config->algo_flow = DC::AlgoFlow::All;
            break;
        }
        case 3: {
            auto val = get_string_from_user("Face selection: ", {"single", "all"},
                                            to_lower(RealSenseID::Description(config->face_selection_policy)));
            config->face_selection_policy = (val == "all") ? DC::FaceSelectionPolicy::All : DC::FaceSelectionPolicy::Single;
            break;
        }
        case 4: {
            auto val = get_string_from_user("Dump mode: ", {"none", "croppedface", "fullframe", "debug"},
                                            to_lower(RealSenseID::Description(config->dump_mode)));
            if (val == "croppedface")
                config->dump_mode = DC::DumpMode::CroppedFace;
            else if (val == "fullframe")
                config->dump_mode = DC::DumpMode::FullFrame;
            else if (val == "debug")
                config->dump_mode = DC::DumpMode::Debug;
            else
                config->dump_mode = DC::DumpMode::None;
            break;
        }
        case 5: {
            auto val = get_string_from_user("Frontal face policy: ", {"strict", "moderate", "none"},
                                            to_lower(RealSenseID::Description(config->frontal_face_policy)));
            if (val == "strict")
                config->frontal_face_policy = DC::FrontalFacePolicy::Strict;
            else if (val == "moderate")
                config->frontal_face_policy = DC::FrontalFacePolicy::Moderate;
            else
                config->frontal_face_policy = DC::FrontalFacePolicy::None;
            break;
        }
        case 6: {
            auto cur = (config->person_motion_mode == DC::PersonMotionMode::Walkthrough) ? "walkthrough" : "static";
            auto val = get_string_from_user("Person motion mode: ", {"static", "walkthrough"}, cur);
            config->person_motion_mode = (val == "walkthrough") ? DC::PersonMotionMode::Walkthrough : DC::PersonMotionMode::Static;
            break;
        }
        case 7: {
            auto val =
                get_string_from_user("Rotation: ", {"0", "90", "180", "270"}, std::to_string(rotation_to_degrees(config->camera_rotation)));
            if (val == "90")
                config->camera_rotation = DC::CameraRotation::Rotation_90_Deg;
            else if (val == "180")
                config->camera_rotation = DC::CameraRotation::Rotation_180_Deg;
            else if (val == "270")
                config->camera_rotation = DC::CameraRotation::Rotation_270_Deg;
            else
                config->camera_rotation = DC::CameraRotation::Rotation_0_Deg;
            break;
        }
        case 8:
            set_field(config->max_spoofs, "Max spoof attempts (0-255): ", 0, 255);
            break;
        case 9:
            set_field(config->match_thresh, "Matching threshold (0-65535): ", 0, 65535);
            break;
        case 10:
            set_field(config->gpio_auth_toggling, "GPIO auth toggling (0/1): ", 0, 1);
            break;
        case 11:
            set_field(config->distance_limit, "Distance limit (0:NoLimit, 1:Short, 2:Mid, 3:Long): ", 0, 3);
            break;
        case 12:
            set_field(config->distance_enabled, "Distance enabled (0/1): ", 0, 1);
            break;
        case 13:
            set_field(config->manual_exposure_time_us, "Exposure time in us (0=auto): ", 0, 65535);
            break;
        case 14:
            set_field(config->manual_gain, "Manual gain (0=auto): ", 0, 65535);
            break;
        case 15:
            set_field(config->rect_enable, "Rectangle enabled (0/1): ", 0, 1);
            break;
        case 16:
            set_field(config->landmarks_enable, "Landmarks enabled (0/1): ", 0, 1);
            break;
        case 17:
            set_field(config->num_rois, "Number of ROIs (1-5): ", 1, 5);
            break;
        case 18:
        case 19:
        case 20:
        case 21:
        case 22: {
            int roi_idx = choice - 18;
            if (roi_idx >= static_cast<int>(config->num_rois))
            {
                std::cout << "ROI[" << roi_idx << "] is not active (num_rois=" << static_cast<int>(config->num_rois)
                          << "). Increase Num ROIs first.\n";
                continue;
            }
            bool portrait = config->camera_rotation == DC::CameraRotation::Rotation_0_Deg ||
                            config->camera_rotation == DC::CameraRotation::Rotation_180_Deg;
            int max_w = portrait ? 1080 : 1920;
            int max_h = portrait ? 1920 : 1080;
            auto& roi = config->detection_rois[roi_idx];
            char prompt[64];
            snprintf(prompt, sizeof(prompt), "ROI[%d] x (0-%d): ", roi_idx, max_w);
            roi.x = static_cast<unsigned short>(get_int_from_user(prompt, 0, max_w, roi.x));
            snprintf(prompt, sizeof(prompt), "ROI[%d] y (0-%d): ", roi_idx, max_h);
            roi.y = static_cast<unsigned short>(get_int_from_user(prompt, 0, max_h, roi.y));
            snprintf(prompt, sizeof(prompt), "ROI[%d] width (1-%d): ", roi_idx, max_w);
            roi.width = static_cast<unsigned short>(get_int_from_user(prompt, 1, max_w, roi.width));
            snprintf(prompt, sizeof(prompt), "ROI[%d] height (1-%d): ", roi_idx, max_h);
            roi.height = static_cast<unsigned short>(get_int_from_user(prompt, 1, max_h, roi.height));
            break;
        }
        }

        // Auto-apply after each change
        {
            auto authenticator = CreateAuthenticator();
            auto status = authenticator->SetDeviceConfig(*config);
            std::cout << "Status: " << status << "\n";
        } // destroy authenticator (release serial port) before potential re-sync

        // Re-read config from device to stay in sync (catches rejected values)
        auto refreshed = query_device_config();
        if (refreshed)
            *config = *refreshed;
    }
}

void sample_loop()
{
    std::string input;
    print_menu();

    for (bool first = true; /**/; first = false)
    {
        if (!first)
        {
            std::cout << "\n[?] for menu, [q] to quit\n";
        }
        std::cout << "> " << std::flush;

        if (!std::getline(std::cin, input))
            continue;

        if (input.size() != 1)
            continue;

        if (input[0] == '?')
        {
            print_menu();
            continue;
        }

        switch (input[0])
        {
        case 'e':
            do_enroll(prompt_user_id().c_str());
            break;
        case 'a':
            do_authenticate();
            break;
        case 't':
            do_authenticate_loop();
            break;
        case 'd':
            remove_users();
            break;
#ifdef RSID_SECURE
        case 'p':
            pair_device();
            break;
        case 'i':
            unpair_device();
            break;
#endif // RSID_SECURE
#ifdef RSID_PREVIEW
        case 'c': {
            RealSenseID::PreviewConfig pconfig;
            pconfig.deviceType = s_device_info.deviceType;
            pconfig.cameraNumber = s_device_info.cameraNumber;
            pconfig.skip_decode = true; // so we get jpeg frames
            s_preview = std::make_unique<RealSenseID::Preview>(pconfig);
            s_preview_callback = std::make_unique<PreviewRender>();
            s_preview->StartPreview(*s_preview_callback);
            std::cout << "starting preview for 3 seconds ";
            std::this_thread::sleep_for(std::chrono::seconds {3});
            s_preview->StopPreview();
            std::this_thread::sleep_for(std::chrono::milliseconds {400});
            std::cout << "\n";
            break;
        }
#endif // RSID_PREVIEW
        case 's':
            configure_device();
            break;
        case 'g':
            show_device_config();
            break;
        case 'u':
            get_users();
            break;
        case 'b':
            standby();
            break;
        case 'h':
            hibernate();
            break;
        case 'v':
            device_info();
            break;
        case 'x': {
            int iters = get_int_from_user("Iterations: ", 1, 999999);
            ping_device(iters);
            break;
        }
        case 'q':
            return;
        case 'E':
            enroll_faceprints(prompt_user_id().c_str());
            break;
        case 'A':
            authenticate_faceprints();
            break;
        case 'U': {
            std::cout << "\n" << s_user_faceprint_db.size() << " users\n";
            for (const auto& [user_id, faceprint] : s_user_faceprint_db)
            {
                std::cout << " * " << user_id << "\n";
            }
            std::cout << "\n";
            break;
        }
        case 'D':
            s_user_faceprint_db.clear();
            std::cout << "\nFaceprints deleted..\n\n";
            break;
        case 'L':
            unlock();
            break;
        case 'o':
            query_log();
            break;
        case 'w':
            color_gains();
            break;
        case 'f':
            get_temperature();
            break;
        case 'R':
            reboot_device();
            break;
        case '0':
            do_detect_persons();
            break;
        case '1':
            do_detect_poses();
            break;
        case '2':
            do_detect_body_parts();
            break;
        case '3':
            do_decode_barcodes();
            break;
        case 'B':
            debug_mode();
            break;
        case 'F':
            save_debug();
            break;
        case 'M':
            mount_debug();
            break;
        }
    }
}

int main()
{
    try
    {
        std::cout << "Discovering devices...\n" << std::flush;
        auto devices = RealSenseID::DiscoverDevices();

        if (devices.empty())
        {
            std::cout << "Error: No rsid devices were found.\n";
            std::exit(1);
        }

        // Auto-select if 1 device, otherwise ask user
        if (devices.size() == 1)
        {
            s_device_info = devices[0];
            std::cout << "Found 1 device. Auto-selecting:\n";
            std::cout << " - Port: " << s_device_info.serialPort << " (S/N: " << s_device_info.serialNumber << ")\n";
        }
        else
        {
            std::cout << "Found " << devices.size() << " devices:\n";
            for (size_t i = 0; i < devices.size(); ++i)
            {
                std::cout << i << ". Port: " << devices[i].serialPort << " \t(S/N: " << devices[i].serialNumber << ")\n";
            }

            auto prompt = "\nSelect device index (0-" + std::to_string(devices.size() - 1) + "): ";
            int selection = get_int_from_user(prompt.c_str(), 0, static_cast<int>(devices.size()) - 1);
            s_device_info = devices[selection];
            std::cout << "Selected: " << s_device_info.serialPort << "\n";
        }

        // Start main loop
        sample_loop();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Exception occurred: " << ex.what() << "\n";
        return 1;
    }
}

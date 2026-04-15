// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include <RealSenseID/Version.h>
#include <RealSenseID/DiscoverDevices.h>
#include <RealSenseID/FaceAuthenticator.h>
#include <RealSenseID/EnrollmentCallback.h>
#include "rsid_py.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <sstream>
#include <iterator>
#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>
#include <type_traits>

// clang-format off
namespace py = pybind11;
using namespace RealSenseID;

///////////////////////////////////////////////////////////////////////////////////
// Enroll Callback support
///////////////////////////////////////////////////////////////////////////////////
using EnrollStatusClbkFun = std::function<void(const EnrollStatus)>;
// using EnrollHintClbkFun = EnrollStatusClbkFun;
using EnrollHintClbkFun = std::function<void(const EnrollStatus, float)>;
using EnrollProgressClbkFun = std::function<void(const FacePose)>;
using Faces = std::vector<FaceRect>;
using Landmarks = std::vector<FaceLandmarks>;
using Persons = std::vector<PersonRect>;
using PersonPoses = std::vector<PersonPose>;
using FaceDetectedClbkFun = std::function<void(const Faces&, const unsigned int)>;
using LandmarksDetectedClbkFun = std::function<void(const Landmarks&, const unsigned int)>;
using FaceDistancesClbkFun = std::function<void(const std::vector<double>&, const unsigned int)>;
using PersonDetectedClbkFun = std::function<void(const Persons&, const unsigned int)>;
using PersonPoseClbkFun = std::function<void(const PersonPoses&, const unsigned int)>;
using BarcodeClbkFun = std::function<void(const std::vector<std::string>&, unsigned int)>;
using FaceCroppedImageClbkFun = std::function<void(py::memoryview, const unsigned int, const unsigned int, const unsigned int)>;

class EnrollCallbackPy : public EnrollmentCallback
{
    EnrollStatusClbkFun& _result_clbk;
    EnrollProgressClbkFun& _progress_clbk;
    EnrollHintClbkFun& _hint_clbk;
    FaceDetectedClbkFun& _face_clbk;
    LandmarksDetectedClbkFun& _lms_clbk;
    FaceCroppedImageClbkFun& _face_cropped_image_clbk;

public:
    EnrollCallbackPy(EnrollStatusClbkFun& result_clbk, EnrollProgressClbkFun& progress_clbk, EnrollHintClbkFun& hint_clbk,
                     FaceDetectedClbkFun& face_clbk, LandmarksDetectedClbkFun& lms_clbk, FaceCroppedImageClbkFun& face_cropped_image_clbk) :
        _result_clbk {result_clbk}, _progress_clbk {progress_clbk}, _hint_clbk {hint_clbk}, _face_clbk {face_clbk}, _lms_clbk {lms_clbk},
        _face_cropped_image_clbk {face_cropped_image_clbk}
    {
    }

    void OnResult(const EnrollStatus status) override
    {
        if (_result_clbk)
        {
            py::gil_scoped_acquire acquire;
            _result_clbk(status);
        }
    }

    void OnProgress(const FacePose pose) override
    {
        if (_progress_clbk)
        {
            py::gil_scoped_acquire acquire;
            _progress_clbk(pose);
        }
    }

    void OnHint(const EnrollStatus hint, float frameScore) override
    {
        if (_hint_clbk)
        {
            py::gil_scoped_acquire acquire;
            _hint_clbk(hint, frameScore);
        }
    }

    void OnFaceDetected(const std::vector<FaceRect>& faces, const unsigned int ts) override
    {
        if (_face_clbk)
        {
            py::gil_scoped_acquire acquire;
            _face_clbk(faces, ts);
        }
    }

    void OnLandmarksDetected(const std::vector<FaceLandmarks>& landmarks, const unsigned int ts) override
    {
        if (_lms_clbk)
        {
            py::gil_scoped_acquire acquire;
            _lms_clbk(landmarks, ts);
        }
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        if (_face_cropped_image_clbk && buffer && width && height)
        {
            py::gil_scoped_acquire acquire;
            const auto buf_size = static_cast<Py_ssize_t>(static_cast<size_t>(width) * height * 3);
            if (buf_size && buf_size > 0)
            {
                const auto mv = py::memoryview::from_memory(const_cast<unsigned char*>(buffer), buf_size, true /* readonly */);
                _face_cropped_image_clbk(mv, width, height, ts);
            }
        }
    }
};


///////////////////////////////////////////////////////////////////////////////////
// Authenticate Callback support
///////////////////////////////////////////////////////////////////////////////////
using AuthStatusClbkFun = std::function<void(const AuthenticateStatus, const char* user_id)>;
using AuthHintClbkFun = std::function<void(const AuthenticateStatus, const float)>;

class AuthCallbackPy : public AuthenticationCallback
{
    AuthStatusClbkFun& _result_clbk;
    AuthHintClbkFun& _hint_clbk;
    FaceDetectedClbkFun& _face_clbk;
    LandmarksDetectedClbkFun& _lms_clbk;
    FaceDistancesClbkFun& _distances_clbk;
    PersonDetectedClbkFun& _person_clbk;
    PersonPoseClbkFun& _person_pose_clbk;
    FaceCroppedImageClbkFun& _face_cropped_image_clbk;
    BarcodeClbkFun& _barcode_clbk;

public:
    AuthCallbackPy(
        AuthStatusClbkFun& result_clbk,
        AuthHintClbkFun& hint_clbk,
        FaceDetectedClbkFun& face_clbk,
        LandmarksDetectedClbkFun& lms_clbk,
        FaceDistancesClbkFun& distances_clbk,
        PersonDetectedClbkFun& person_clbk,
        PersonPoseClbkFun& person_pose_clbk,
        FaceCroppedImageClbkFun& face_cropped_image_clbk,
        BarcodeClbkFun& barcode_clbk) :
        _result_clbk {result_clbk},
        _hint_clbk {hint_clbk},
        _face_clbk {face_clbk},
        _lms_clbk {lms_clbk},
        _distances_clbk {distances_clbk},
        _person_clbk {person_clbk},
        _person_pose_clbk(person_pose_clbk),
        _face_cropped_image_clbk {face_cropped_image_clbk},
        _barcode_clbk {barcode_clbk} {}

    void OnResult(const AuthenticateStatus status, const char* user_id, short score) override
    {
        if (_result_clbk)
        {
            py::gil_scoped_acquire acquire;
            _result_clbk(status, user_id);
        }
    }


    void OnHint(const AuthenticateStatus hint, float frameScore) override
    {
        if (_hint_clbk)
        {
            py::gil_scoped_acquire acquire;
            _hint_clbk(hint, frameScore);
        }
    }

    void OnFaceDetected(const std::vector<FaceRect>& faces, const unsigned int ts) override
    {
        if (_face_clbk)
        {
            py::gil_scoped_acquire acquire;
            _face_clbk(faces, ts);
        }
    }

    void OnLandmarksDetected(const std::vector<FaceLandmarks>& landmarks, const unsigned int ts) override
    {
        if (_lms_clbk)
        {
            py::gil_scoped_acquire acquire;
            _lms_clbk(landmarks, ts);
        }
    }

    void OnFaceDistances(const std::vector<double>& distances, const unsigned int ts) override
    {
        if (_distances_clbk)
        {
            py::gil_scoped_acquire acquire;
            _distances_clbk(distances, ts);
        }
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        if (_face_cropped_image_clbk && buffer && width && height)
        {
            py::gil_scoped_acquire acquire;
            const auto buf_size = static_cast<Py_ssize_t>(static_cast<size_t>(width) * height * 3);
            if (buf_size && buf_size > 0)
            {
                const auto mv = py::memoryview::from_memory(const_cast<unsigned char*>(buffer), buf_size, true /* readonly */);
                _face_cropped_image_clbk(mv, width, height, ts);
            }
        }
    }
};


///////////////////////////////////////////////////////////////////////////////////
// Faceprints Enroll Callback support
///////////////////////////////////////////////////////////////////////////////////

using FaceprintsEnrollStatusClbkFun = std::function<void(const EnrollStatus, const ExtractedFaceprintsElement* faceprints)>;


class FaceprintsEnrollCallbackPy : public EnrollFaceprintsExtractionCallback
{
    FaceprintsEnrollStatusClbkFun& _result_clbk;
    EnrollProgressClbkFun& _progress_clbk;
    EnrollHintClbkFun& _hint_clbk;
    FaceDetectedClbkFun& _face_clbk;
    LandmarksDetectedClbkFun& _lms_clbk;
    FaceCroppedImageClbkFun& _face_cropped_image_clbk;

public:
    FaceprintsEnrollCallbackPy(FaceprintsEnrollStatusClbkFun& result_clbk, EnrollProgressClbkFun& progress_clbk,
                               EnrollHintClbkFun& hint_clbk, FaceDetectedClbkFun& face_clbk, LandmarksDetectedClbkFun& lms_clbk,
                               FaceCroppedImageClbkFun& face_cropped_image_clbk) :
        _result_clbk {result_clbk}, _progress_clbk {progress_clbk}, _hint_clbk {hint_clbk}, _face_clbk {face_clbk}, _lms_clbk {lms_clbk},
        _face_cropped_image_clbk {face_cropped_image_clbk}
    {
    }

    void OnResult(const EnrollStatus status, const ExtractedFaceprints* faceprints) override
    {
        if (_result_clbk)
        {
            py::gil_scoped_acquire acquire;
            if (faceprints != nullptr)
                _result_clbk(status, &faceprints->data);
            else
                _result_clbk(status, nullptr);
        }
    }

    void OnProgress(const FacePose pose) override
    {
        if (_progress_clbk)
        {
            py::gil_scoped_acquire acquire;
            _progress_clbk(pose);
        }
    }


    void OnHint(const EnrollStatus hint, float frameScore) override
    {
        if (_hint_clbk)
        {
            py::gil_scoped_acquire acquire;
            _hint_clbk(hint, frameScore);
        }
    }

    void OnFaceDetected(const std::vector<FaceRect>& faces, const unsigned int ts) override
    {
        if (_face_clbk)
        {
            py::gil_scoped_acquire acquire;
            _face_clbk(faces, ts);
        }
    }

    void OnLandmarksDetected(const std::vector<FaceLandmarks>& landmarks, const unsigned int ts) override
    {
        if (_lms_clbk)
        {
            py::gil_scoped_acquire acquire;
            _lms_clbk(landmarks, ts);
        }
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        if (_face_cropped_image_clbk && buffer && width && height)
        {
            py::gil_scoped_acquire acquire;
            const auto buf_size = static_cast<Py_ssize_t>(static_cast<size_t>(width) * height * 3);
            if (buf_size && buf_size > 0)
            {
                const auto mv = py::memoryview::from_memory(const_cast<unsigned char*>(buffer), buf_size, true /* readonly */);
                _face_cropped_image_clbk(mv, width, height, ts);
            }
        }
    }
};
///////////////////////////////////////////////////////////////////////////////////
// Faceprints Auth Callback support
///////////////////////////////////////////////////////////////////////////////////
using FaceprintsAuthStatusClbkFun = std::function<void(const AuthenticateStatus, const ExtractedFaceprintsElement* faceprints)>;

class FaceprintsAuthCallbackPy : public AuthFaceprintsExtractionCallback
{
    FaceprintsAuthStatusClbkFun& _result_clbk;
    AuthHintClbkFun& _hint_clbk;
    FaceDetectedClbkFun& _face_clbk;
    LandmarksDetectedClbkFun& _lms_clbk;
    FaceCroppedImageClbkFun& _face_cropped_image_clbk;

public:
    FaceprintsAuthCallbackPy(FaceprintsAuthStatusClbkFun& result_clbk,
        AuthHintClbkFun& hint_clbk,
        FaceDetectedClbkFun& face_clbk,
        LandmarksDetectedClbkFun& lms_clbk,
        FaceCroppedImageClbkFun& face_cropped_image_clbk) :
        _result_clbk {result_clbk},
        _hint_clbk {hint_clbk},
        _face_clbk {face_clbk},
        _lms_clbk {lms_clbk},
        _face_cropped_image_clbk {face_cropped_image_clbk}
    {
    }

    void OnResult(const AuthenticateStatus status, const ExtractedFaceprints* faceprints) override
    {
        if (_result_clbk)
        {
            py::gil_scoped_acquire acquire;
            if (faceprints != nullptr)
                _result_clbk(status, &faceprints->data);
            else
                _result_clbk(status, nullptr);
        }
    }

    void OnHint(const AuthenticateStatus hint, float frameScore) override
    {
        if (_hint_clbk)
        {
            py::gil_scoped_acquire acquire;
            _hint_clbk(hint, frameScore);
        }
    }

    void OnFaceDetected(const std::vector<FaceRect>& faces, const unsigned int ts) override
    {
        if (_face_clbk)
        {
            py::gil_scoped_acquire acquire;
            _face_clbk(faces, ts);
        }
    }

    void OnLandmarksDetected(const std::vector<FaceLandmarks>& landmarks, const unsigned int ts) override
    {
        if (_lms_clbk)
        {
            py::gil_scoped_acquire acquire;
            _lms_clbk(landmarks, ts);
        }
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        if (_face_cropped_image_clbk && buffer && width && height)
        {
            py::gil_scoped_acquire acquire;
            const auto buf_size = static_cast<Py_ssize_t>(static_cast<size_t>(width) * height * 3);
            if (buf_size && buf_size > 0)
            {
                const auto mv = py::memoryview::from_memory(const_cast<unsigned char*>(buffer), buf_size, true /* readonly */);
                _face_cropped_image_clbk(mv, width, height, ts);
            }
        }
    }
};


// query users and return as std::vector
static std::vector<std::string> query_users(FaceAuthenticator& authenticator)
{
    std::vector<std::string> rv;
    unsigned int number_of_users = 0;
    auto status = authenticator.QueryNumberOfUsers(number_of_users);
    if (status != RealSenseID::Status::Ok)
    {
        throw std::runtime_error(std::string("QueryNumberOfUsers: ") + Description(status));
    }

    if (number_of_users == 0)
    {
        return rv;
    }

    // allocate needed arrays of user ids
    std::vector<std::array<char, RealSenseID::MAX_USERID_LENGTH>> arrays {number_of_users};

    std::vector<char*> ptrs;
    for (auto& ptr : arrays)
    {
        ptrs.push_back(ptr.data());
    }
    unsigned int nusers_in_out = number_of_users;
    status = authenticator.QueryUserIds(ptrs.data(), nusers_in_out);
    if (status != RealSenseID::Status::Ok)
    {
        throw std::runtime_error(std::string("QueryUserIds: ") + Description(status));
    }

    // convert to vector of strings
    rv.reserve(nusers_in_out);
    for (unsigned int i = 0; i < nusers_in_out; i++)
    {
        ptrs[i][RealSenseID::MAX_USERID_LENGTH - 1] = '\0';
        rv.push_back(std::string(ptrs[i]));
    }
    return rv;
}


std::ostream& operator<<(std::ostream& oss, const feature_t features[RSID_FEATURES_VECTOR_ALLOC_SIZE])
{
    oss << '[';
    for (size_t i = 0; i < RSID_FEATURES_VECTOR_ALLOC_SIZE; i++)
    {
        oss << features[i];
        if (i < RSID_FEATURES_VECTOR_ALLOC_SIZE - 1)
            oss << ", ";
    }
    oss << ']';
    return oss;
}

void init_face_authenticator(pybind11::module& m)
{
    m.attr("RSID_FACEPRINTS_VERSION") = RSID_FACEPRINTS_VERSION;
    m.attr("RSID_FEATURES_VECTOR_ALLOC_SIZE") = RSID_FEATURES_VECTOR_ALLOC_SIZE;
    m.attr("RSID_NUM_OF_RECOGNITION_FEATURES") = RSID_NUM_OF_RECOGNITION_FEATURES;

    py::enum_<DeviceType>(m, "DeviceType")
        .value("Unknown", DeviceType::Unknown)
        .value("F45x", DeviceType::F45x)
        .value("F50x", DeviceType::F50x);

    py::enum_<Status>(m, "Status")
        .value("Ok", Status::Ok)
        .value("Error", Status::Error)
        .value("SerialError", Status::SerialError)
        .value("SecurityError", Status::SecurityError)
        .value("VersionMismatch", Status::VersionMismatch)
        .value("CrcError", Status::CrcError)
        .value("TooManySpoofs", Status::TooManySpoofs)
        .value("NotSupported", Status::NotSupported)
        .value("DatabaseFull", Status::DatabaseFull)
        .value("DuplicateUserId", Status::DuplicateUserId)
        .value("DuplicateFaceprints", Status::DuplicateFaceprints)
        .value("InvalidSettings", Status::InvalidSettings);

    py::enum_<AuthenticateStatus>(m, "AuthenticateStatus")
        .value("Success", AuthenticateStatus::Success)
        .value("NoFaceDetected", AuthenticateStatus::NoFaceDetected)
        .value("FaceDetected", AuthenticateStatus::FaceDetected)
        .value("NoPersonDetected", AuthenticateStatus::PersonNotFound)
        .value("PersonDetected", AuthenticateStatus::PersonFound)
        .value("BarcodeNotFound", AuthenticateStatus::BarcodeNotFound)
        .value("BarcodeFound", AuthenticateStatus::BarcodeFound)
        .value("LedFlowSuccess", AuthenticateStatus::LedFlowSuccess)
        .value("FaceIsTooFarToTheTop", AuthenticateStatus::FaceIsTooFarToTheTop)
        .value("FaceIsTooFarToTheBottom", AuthenticateStatus::FaceIsTooFarToTheBottom)
        .value("FaceIsTooFarToTheRight", AuthenticateStatus::FaceIsTooFarToTheRight)
        .value("FaceIsTooFarToTheLeft", AuthenticateStatus::FaceIsTooFarToTheLeft)
        .value("FaceTiltIsTooUp", AuthenticateStatus::FaceTiltIsTooUp)
        .value("FaceTiltIsTooDown", AuthenticateStatus::FaceTiltIsTooDown)
        .value("FaceTiltIsTooRight", AuthenticateStatus::FaceTiltIsTooRight)
        .value("FaceTiltIsTooLeft", AuthenticateStatus::FaceTiltIsTooLeft)
        .value("FaceIsNotFrontal", AuthenticateStatus::FaceIsNotFrontal)
        .value("CameraStarted", AuthenticateStatus::CameraStarted)
        .value("CameraStopped", AuthenticateStatus::CameraStopped)
        .value("Spoof", AuthenticateStatus::Spoof)
        .value("Forbidden", AuthenticateStatus::Forbidden)
        .value("DeviceError", AuthenticateStatus::DeviceError)
        .value("Failure", AuthenticateStatus::Failure)
        .value("TooManySpoofs", AuthenticateStatus::TooManySpoofs)
        .value("InvalidFeatures", AuthenticateStatus::InvalidFeatures)
        .value("Ok", AuthenticateStatus::Ok)
        .value("Error", AuthenticateStatus::Error)
        .value("SerialError", AuthenticateStatus::SerialError)
        .value("SecurityError", AuthenticateStatus::SecurityError)
        .value("VersionMismatch", AuthenticateStatus::VersionMismatch)
        .value("CrcError", AuthenticateStatus::CrcError)
        .value("Spoof_2D", AuthenticateStatus::Spoof_2D)
        .value("Spoof_3D", AuthenticateStatus::Spoof_3D)
        .value("Spoof_LR", AuthenticateStatus::Spoof_LR)
        .value("Spoof_Surface", AuthenticateStatus::Spoof_Surface)
        .value("Spoof_Disparity", AuthenticateStatus::Spoof_Disparity)
        .value("Spoof_Vision", AuthenticateStatus::Spoof_Vision)
        .value("Spoof_2D_Right", AuthenticateStatus::Spoof_2D_Right)
        .value("Spoof_Plane_Disparity", AuthenticateStatus::Spoof_Plane_Disparity)
        .value("Sunglasses", AuthenticateStatus::Sunglasses)
        .value("MedicalMask", AuthenticateStatus::MedicalMask)
        .value("FaceTooFar", AuthenticateStatus::FaceTooFar)
        .value("CalcDistanceFailure", AuthenticateStatus::CalcDistanceFailure)
        .value("FaceTooClose", AuthenticateStatus::FaceTooClose);

    py::enum_<EnrollStatus>(m, "EnrollStatus")
        .value("Success", EnrollStatus::Success)
        .value("NoFaceDetected", EnrollStatus::NoFaceDetected)
        .value("FaceDetected", EnrollStatus::FaceDetected)
        .value("PersonNotFound", EnrollStatus::PersonNotFound)
        .value("PersonFound", EnrollStatus::PersonFound)
        .value("BarcodeNotFound", EnrollStatus::BarcodeNotFound)
        .value("BarcodeFound", EnrollStatus::BarcodeFound)
        .value("LedFlowSuccess", EnrollStatus::LedFlowSuccess)
        .value("FaceIsTooFarToTheTop", EnrollStatus::FaceIsTooFarToTheTop)
        .value("FaceIsTooFarToTheBottom", EnrollStatus::FaceIsTooFarToTheBottom)
        .value("FaceIsTooFarToTheRight", EnrollStatus::FaceIsTooFarToTheRight)
        .value("FaceIsTooFarToTheLeft", EnrollStatus::FaceIsTooFarToTheLeft)
        .value("FaceTiltIsTooUp", EnrollStatus::FaceTiltIsTooUp)
        .value("FaceTiltIsTooDown", EnrollStatus::FaceTiltIsTooDown)
        .value("FaceTiltIsTooRight", EnrollStatus::FaceTiltIsTooRight)
        .value("FaceTiltIsTooLeft", EnrollStatus::FaceTiltIsTooLeft)
        .value("FaceIsNotFrontal", EnrollStatus::FaceIsNotFrontal)
        .value("CameraStarted", EnrollStatus::CameraStarted)
        .value("CameraStopped", EnrollStatus::CameraStopped)
        .value("MultipleFacesDetected", EnrollStatus::MultipleFacesDetected)
        .value("Failure", EnrollStatus::Failure)
        .value("DeviceError", EnrollStatus::DeviceError)
        .value("Spoof", EnrollStatus::Spoof)
        .value("InvalidFeatures", EnrollStatus::InvalidFeatures)
        .value("Ambiguous ", EnrollStatus::AmbiguousFace)
        .value("Ok", EnrollStatus::Ok)
        .value("Error", EnrollStatus::Error)
        .value("SerialError", EnrollStatus::SerialError)
        .value("SecurityError", EnrollStatus::SecurityError)
        .value("VersionMismatch", EnrollStatus::VersionMismatch)
        .value("CrcError", EnrollStatus::CrcError)
        .value("TooManySpoofs", EnrollStatus::TooManySpoofs)
        .value("NotSupported", EnrollStatus::NotSupported)
        .value("DatabaseFull", EnrollStatus::DatabaseFull)
        .value("DuplicateUserId", EnrollStatus::DuplicateUserId)
        .value("DuplicateFaceprints", EnrollStatus::DuplicateFaceprints)
        .value("Spoof_2D", EnrollStatus::Spoof_2D)
        .value("Spoof_3D", EnrollStatus::Spoof_3D)
        .value("Spoof_LR", EnrollStatus::Spoof_LR)
        .value("Spoof_Surface", EnrollStatus::Spoof_Surface)
        .value("Spoof_Disparity", EnrollStatus::Spoof_Disparity)
        .value("Spoof_Vision", EnrollStatus::Spoof_Vision)
        .value("Spoof_2D_Right", EnrollStatus::Spoof_2D_Right)
        .value("Spoof_Plane_Disparity", EnrollStatus::Spoof_Plane_Disparity)
        .value("Sunglasses", EnrollStatus::Sunglasses)
        .value("MedicalMask", EnrollStatus::MedicalMask)
        .value("FaceTooClose", EnrollStatus::FaceTooClose);


    py::enum_<FacePose>(m, "FacePose")
        .value("Center", FacePose::Center)
        .value("Up", FacePose::Up)
        .value("Down", FacePose::Down)
        .value("Left", FacePose::Left)
        .value("Right", FacePose::Right);

    py::class_<FaceRect>(m, "FaceRect")
        .def("__copy__", [](const FaceRect& self) { return FaceRect(self); })
        .def(py::init<>())
        .def_readwrite("x", &FaceRect::x)
        .def_readwrite("y", &FaceRect::y)
        .def_readwrite("w", &FaceRect::w)
        .def_readwrite("h", &FaceRect::h)
        .def("__repr__", [](const FaceRect& f) {
            std::ostringstream oss;
            oss << "<rsid_py.FaceRect " << f.x << ',' << f.y << ' ' << f.w << 'x' << f.y << '>';
            return oss.str();
        });

    py::enum_<PersonRect::BodyPart>(m, "BodyPart")
        .value("Person", PersonRect::BodyPart::Person)
        .value("Foot", PersonRect::BodyPart::Foot)
        .value("Arm", PersonRect::BodyPart::Arm)
        .value("Leg", PersonRect::BodyPart::Leg)
        .value("Hand", PersonRect::BodyPart::Hand)
        .value("Torso", PersonRect::BodyPart::Torso);

    py::class_<FaceLandmarks>(m, "FaceLandmarks")
        .def(py::init<>())
        .def("__copy__", [](const FaceLandmarks& self) { return FaceLandmarks(self); })
        /* lm_x getter/setter */
        .def_property(
            "lm_x",
            [](FaceLandmarks& self) {
                // getter - return as vector of uint32_t
                return std::vector<uint32_t> {std::begin(self.lm_x), std::end(self.lm_x)};
            },
            // setter of lm_x list
            [](FaceLandmarks& self, const std::vector<uint32_t>& new_lm_x) {
                constexpr size_t n_elements = std::extent<decltype(self.lm_x)>::value;
                static_assert(n_elements == NUM_FACE_LANDMARKS, "n_elements!=NUM_FACE_LANDMARKS");
                if (new_lm_x.size() != n_elements)
                {
                    throw std::runtime_error("Invalid number of elements in setter. Expected " + std::to_string(n_elements));
                }
                std::copy(new_lm_x.begin(), new_lm_x.end(), self.lm_x);
            })

        /* lm_y getter/setter */
        .def_property(
            "lm_y",
            [](FaceLandmarks& self) {
                // getter - return as vector of uint32_t
                return std::vector<uint32_t> {std::begin(self.lm_y), std::end(self.lm_y)};
            },
            // setter of avg descriptor list
            [](FaceLandmarks& self, const std::vector<uint32_t>& new_lm_y) {
                constexpr size_t n_elements = std::extent<decltype(self.lm_y)>::value;
                static_assert(n_elements == NUM_FACE_LANDMARKS, "n_elements!=NUM_FACE_LANDMARKS");
                if (new_lm_y.size() != n_elements)
                {
                    throw std::runtime_error("Invalid number of elements in setter. Expected " + std::to_string(n_elements));
                }
                std::copy(new_lm_y.begin(), new_lm_y.end(), self.lm_y);
            })

        .def("__repr__", [](const FaceLandmarks& fp) {
            std::ostringstream oss;
            oss << "<rsid_py.FaceLandmarks "
                << "lm_x[0]" << fp.lm_x[0] << ", "
                << "lm_x[1]" << fp.lm_x[1] << ", "
                << "lm_x[2]" << fp.lm_x[2] << ", "
                << "lm_x[3]" << fp.lm_x[3] << ", "
                << "lm_x[4]" << fp.lm_x[4] << ", "
                << "lm_y[0]" << fp.lm_y[0] << ", "
                << "lm_y[1]" << fp.lm_y[1] << ", "
                << "lm_y[2]" << fp.lm_y[2] << ", "
                << "lm_y[3]" << fp.lm_y[3] << ", "
                << "lm_y[4]" << fp.lm_y[4] << '>';
            return oss.str();
        });

    py::class_<PersonRect>(m, "PersonRect")
        .def("__copy__", [](const PersonRect& self) { return PersonRect(self); })
        .def(py::init<>())
        .def_readwrite("x", &PersonRect::x)
        .def_readwrite("y", &PersonRect::y)
        .def_readwrite("w", &PersonRect::w)
        .def_readwrite("h", &PersonRect::h)
        .def_readwrite("id", &PersonRect::id)
        .def_readwrite("distance", &PersonRect::distance)
        .def_readwrite("body_part", &PersonRect::body_part)

        .def("__repr__", [](const PersonRect& p) {
            const std::string part = py::str(py::cast(p.body_part).attr("name"));
            std::ostringstream oss;
            oss << "<rsid_py.PersonRect id=" << p.id << " x=" << p.x << " y=" << p.y << " w=" << p.w << " h=" << p.h
                << " body_part=" << part << " distance=" << p.distance << '>';
            return oss.str();
        });

    py::class_<PersonPose>(m, "PersonPose")
        .def("__copy__", [](const PersonPose& self) { return PersonPose(self); })
        .def(py::init<>())
        .def_readwrite("x", &PersonPose::x)
        .def_readwrite("y", &PersonPose::y)
        .def_readwrite("w", &PersonPose::w)
        .def_readwrite("h", &PersonPose::h)
        .def_property_readonly("landmarks",
                               [](const PersonPose& self) {
                                   py::list out;
                                   for (size_t i = 0; i < NUM_POSE_LANDMARKS; ++i)
                                       out.append(py::make_tuple(self.lm_x[i], self.lm_y[i], self.lm_score[i]));
                                   return out;
                               })

        .def("__repr__", [](const PersonPose& p) {
            std::ostringstream oss;
            oss << "<rsid_py.PersonPose x=" << p.x << " y=" << p.y << " w=" << p.w << " h=" << p.h << '>';
            return oss.str();
        });

    py::enum_<DeviceConfig::CameraRotation>(m, "CameraRotation")
        .value("Rotation_0_Deg", DeviceConfig::CameraRotation::Rotation_0_Deg)
        .value("Rotation_180_Deg", DeviceConfig::CameraRotation::Rotation_180_Deg)
        .value("Rotation_90_Deg", DeviceConfig::CameraRotation::Rotation_90_Deg)
        .value("Rotation_270_Deg", DeviceConfig::CameraRotation::Rotation_270_Deg);

    py::enum_<DeviceConfig::SecurityLevel>(m, "SecurityLevel")
        .value("High", DeviceConfig::SecurityLevel::High)
        .value("Medium", DeviceConfig::SecurityLevel::Medium)
        .value("Low", DeviceConfig::SecurityLevel::Low);

    py::enum_<DeviceConfig::MatcherConfidenceLevel>(m, "MatcherConfidenceLevel")
        .value("High", DeviceConfig::MatcherConfidenceLevel::High)
        .value("Medium", DeviceConfig::MatcherConfidenceLevel::Medium)
        .value("Low", DeviceConfig::MatcherConfidenceLevel::Low);

    py::enum_<DeviceConfig::AlgoFlow>(m, "AlgoFlow")
        .value("All", DeviceConfig::AlgoFlow::All)
        .value("FaceDetectionOnly", DeviceConfig::AlgoFlow::FaceDetectionOnly)
        .value("SpoofOnly", DeviceConfig::AlgoFlow::SpoofOnly)
        .value("RecognitionOnly", DeviceConfig::AlgoFlow::RecognitionOnly);

    py::enum_<DeviceConfig::DumpMode>(m, "DumpMode")
        .value("Disable", DeviceConfig::DumpMode::None)
        .value("CroppedFace", DeviceConfig::DumpMode::CroppedFace)
        .value("FullFrame", DeviceConfig::DumpMode::FullFrame)
        .value("Debug", DeviceConfig::DumpMode::Debug);

    py::enum_<DeviceConfig::FaceSelectionPolicy>(m, "FaceSelectionPolicy")
        .value("Single", DeviceConfig::FaceSelectionPolicy::Single)
        .value("All", DeviceConfig::FaceSelectionPolicy::All);

    py::enum_<DeviceConfig::FrontalFacePolicy>(m, "FrontalFacePolicy")
        .value("None", DeviceConfig::FrontalFacePolicy::None)
        .value("Moderate", DeviceConfig::FrontalFacePolicy::Moderate)
        .value("Strict", DeviceConfig::FrontalFacePolicy::Strict);

    py::enum_<DeviceConfig::DistanceLimit>(m, "DistanceLimit")
        .value("NoLimit", DeviceConfig::DistanceLimit::NoLimit)
        .value("Short", DeviceConfig::DistanceLimit::Short)
        .value("Mid", DeviceConfig::DistanceLimit::Mid)
        .value("Long", DeviceConfig::DistanceLimit::Long);

    py::enum_<DeviceConfig::PersonMotionMode>(m, "PersonMotionMode")
        .value("Static", DeviceConfig::PersonMotionMode::Static)
        .value("Walkthrough", DeviceConfig::PersonMotionMode::Walkthrough);

    py::class_<DeviceConfig::Roi>(m, "Roi")
    .def(py::init<>())
    .def_readwrite("x", &DeviceConfig::Roi::x)
    .def_readwrite("y", &DeviceConfig::Roi::y)
    .def_readwrite("width", &DeviceConfig::Roi::width)
    .def_readwrite("height", &DeviceConfig::Roi::height)
    .def("__repr__", [](const DeviceConfig::Roi& r) {
        std::ostringstream oss;
        oss << "<rsid_py.Roi x=" << r.x << " y=" << r.y << " width=" << r.width << " height=" << r.height << '>';
        return oss.str();
    });

    py::class_<DeviceConfig>(m, "DeviceConfig")
        .def(py::init<>())
        .def("__copy__", [](const DeviceConfig& self) { return DeviceConfig(self); })
        .def_readwrite("camera_rotation", &DeviceConfig::camera_rotation)
        .def_readwrite("security_level", &DeviceConfig::security_level)
        .def_readwrite("algo_flow", &DeviceConfig::algo_flow)
        .def_readwrite("face_selection_policy", &DeviceConfig::face_selection_policy)
        .def_readwrite("dump_mode", &DeviceConfig::dump_mode)
        .def_readwrite("matcher_confidence_level", &DeviceConfig::matcher_confidence_level)
        .def_readwrite("max_spoofs", &DeviceConfig::max_spoofs)
        .def_readwrite("auth_gpio_auth_toggling", &DeviceConfig::gpio_auth_toggling)
        .def_readwrite("frontal_face_policy", &DeviceConfig::frontal_face_policy)
        .def_readwrite("manual_exposure_time_us", &DeviceConfig::manual_exposure_time_us)
        .def_readwrite("manual_gain", &DeviceConfig::manual_gain)
        .def_readwrite("distance_limit", &DeviceConfig::distance_limit)
        .def_readwrite("person_motion_mode", &DeviceConfig::person_motion_mode)
        .def_readwrite("match_thresh", &DeviceConfig::match_thresh)
        .def_readwrite("rect_enable", &DeviceConfig::rect_enable)
        .def_readwrite("landmarks_enable", &DeviceConfig::landmarks_enable)
        .def_property(
            "detection_roi",
            [](const DeviceConfig& cfg) { return cfg.detection_rois[0]; },
            [](DeviceConfig& cfg, const DeviceConfig::Roi& roi) { cfg.detection_rois[0] = roi; })
        .def_property(
            "detection_rois",
            [](const DeviceConfig& cfg) {
                std::vector<DeviceConfig::Roi> v(cfg.detection_rois, cfg.detection_rois + cfg.num_rois);
                return v;
            },
            [](DeviceConfig& cfg, const std::vector<DeviceConfig::Roi>& rois) {
                cfg.num_rois = static_cast<unsigned char>(std::min(rois.size(), static_cast<size_t>(DeviceConfig::MAX_ROIS)));
                for (unsigned char i = 0; i < cfg.num_rois; i++)
                    cfg.detection_rois[i] = rois[i];
            })
        .def_readwrite("num_rois", &DeviceConfig::num_rois)
        .def_readwrite("distance_enabled", &DeviceConfig::distance_enabled)

        .def("__repr__", [](const DeviceConfig& cfg) {
            std::ostringstream oss;
            oss << "<rsid_py.DeviceConfig "
                << "camera_rotation=" << cfg.camera_rotation << ", "
                << "security_level=" << cfg.security_level << ", "
                << "matcher_confidence_level=" << cfg.matcher_confidence_level << ", "
                << "algo_flow=" << cfg.algo_flow << ", "
                << "face_selection_policy=" << cfg.face_selection_policy << ", "
                << "dump_mode=" << cfg.dump_mode << ", "
                << "max_spoofs=" << static_cast<unsigned>(cfg.max_spoofs) << ", "
                << "auth_gpio_auth_toggling = " << cfg.gpio_auth_toggling << ", "
                << "frontal_face_policy = " << cfg.frontal_face_policy << ", "
                << "distance_limit = " << cfg.distance_limit << ", "
                << "person_motion_mode = " << static_cast<int>(cfg.person_motion_mode) << ", "
                << "match_thresh = " << cfg.match_thresh << ", "
                << "rect_enable = " << static_cast<unsigned>(cfg.rect_enable) << ", "
                << "landmarks_enable = " << static_cast<unsigned>(cfg.landmarks_enable) << ", "
                << "num_rois = " << static_cast<unsigned>(cfg.num_rois) << ", "
                << "detection_rois[0] = {x=" << cfg.detection_rois[0].x
                << ", y=" << cfg.detection_rois[0].y
                << ", width=" << cfg.detection_rois[0].width
                << ", height=" << cfg.detection_rois[0].height
                << "}, "
                << "distance_enable = " << static_cast<unsigned>(cfg.distance_enabled) << '>';
            return oss.str();
        });

    py::enum_<FaceprintsType>(m, "FaceprintsType").value("W10", FaceprintsType::W10).value("RGB", FaceprintsType::RGB);

    py::class_<DBFaceprintsElement>(m, "Faceprints")
        .def(py::init<>())
        .def("__copy__", [](const DBFaceprintsElement& self) { return DBFaceprintsElement(self); })
        .def_readwrite("version", &DBFaceprintsElement::version)
        .def_readwrite("features_type", &DBFaceprintsElement::featuresType)
        .def_readwrite("flags", &DBFaceprintsElement::flags)
        /* adaptiveDescriptorWithoutMask getter/getter */
        .def_property(
            "adaptive_descriptor_nomask",
            [](DBFaceprintsElement& self) {
                // getter - return as vector of feature_t
                return std::vector<feature_t> {std::begin(self.adaptiveDescriptorWithoutMask),
                                               std::end(self.adaptiveDescriptorWithoutMask)};
            },
            // setter of avg descriptor list
            [](DBFaceprintsElement& self, const std::vector<feature_t>& new_descriptors) {
                constexpr size_t n_elements = std::extent<decltype(self.adaptiveDescriptorWithoutMask)>::value;
                if (new_descriptors.size() != n_elements)
                {
                    throw std::runtime_error("Invalid number of elements in setter. Expected " + std::to_string(n_elements));
                }
                std::copy(new_descriptors.begin(), new_descriptors.end(), self.adaptiveDescriptorWithoutMask);
            })

        /* adaptiveDescriptorEnrollment getter/getter */
        .def_property(
            "enroll_descriptor",
            [](DBFaceprintsElement& self) {
                // getter - return as vector of feature_t
                return std::vector<feature_t> {std::begin(self.enrollmentDescriptor), std::end(self.enrollmentDescriptor)};
            },
            // setter of avg descriptor list
            [](DBFaceprintsElement& self, const std::vector<feature_t>& new_descriptors) {
                constexpr size_t n_elements = std::extent<decltype(self.enrollmentDescriptor)>::value;
                static_assert(n_elements == RSID_FEATURES_VECTOR_ALLOC_SIZE, "n_elements!=RSID_FEATURES_VECTOR_ALLOC_SIZE");
                if (new_descriptors.size() != n_elements)
                {
                    throw std::runtime_error("Invalid number of elements in setter. Expected " + std::to_string(n_elements));
                }
                std::copy(new_descriptors.begin(), new_descriptors.end(), self.enrollmentDescriptor);
            })

        /* reserved array getter/getter */
        .def_property(
            "reserved",
            [](DBFaceprintsElement& self) {
                // getter - return as vector of feature_t
                return std::vector<int> {std::begin(self.reserved), std::end(self.reserved)};
            },
            // setter of avg descriptor list
            [](DBFaceprintsElement& self, const std::vector<feature_t>& new_descriptors) {
                constexpr size_t n_elements = std::extent<decltype(self.reserved)>::value;
                if (new_descriptors.size() != n_elements)
                {
                    throw std::runtime_error("Invalid number of elements in setter. Expected " + std::to_string(n_elements));
                }
                std::copy(new_descriptors.begin(), new_descriptors.end(), self.reserved);
            })
        .def("__repr__", [](const DBFaceprintsElement& fp) {
            std::ostringstream oss;
            oss << "<rsid_py.Faceprints "
                << "version=" << fp.version << ", "
                << "feature_type=" << fp.featuresType << ", "
                << "flags=" << fp.flags << ", "
                << "adaptive_descriptor_nomask: " << fp.adaptiveDescriptorWithoutMask << ", "
                << "enroll_descriptor addr=0x" << fp.enrollmentDescriptor << ", " << '>';
            return oss.str();
        });


    py::class_<ExtractedFaceprintsElement>(m, "ExtractedFaceprintsElement")
        .def(py::init<>())
        .def("__copy__", [](const ExtractedFaceprintsElement& self) { return ExtractedFaceprintsElement(self); })
        .def_readwrite("version", &ExtractedFaceprintsElement::version)
        .def_readwrite("features_type", &ExtractedFaceprintsElement::featuresType)
        .def_readwrite("flags", &ExtractedFaceprintsElement::flags)
        /* featuresVector getter/getter */
        .def_property(
            "features",
            [](ExtractedFaceprintsElement& self) {
                // getter
                return std::vector<feature_t> {std::begin(self.featuresVector), std::end(self.featuresVector)};
            },
            // setter
            [](ExtractedFaceprintsElement& self, const std::vector<feature_t>& new_descriptors) {
                constexpr size_t n_elements = std::extent<decltype(self.featuresVector)>::value;
                static_assert(n_elements == RSID_FEATURES_VECTOR_ALLOC_SIZE, "n_elements!=RSID_FEATURES_VECTOR_ALLOC_SIZE");
                if (new_descriptors.size() != n_elements)
                {
                    throw std::runtime_error("Invalid number of elements in setter. Expected " + std::to_string(n_elements));
                }
                std::copy(new_descriptors.begin(), new_descriptors.end(), self.featuresVector);
            })


        .def("__repr__", [](const ExtractedFaceprintsElement& fp) {
            std::ostringstream oss;
            oss << "<rsid_py.ExtractedFaceprintsElement "
                << "version=" << fp.version << ", "
                << "feature_type=" << fp.featuresType << ", "
                << "flags=" << fp.flags << ", "
                << "features=" << fp.featuresVector << '>';
            return oss.str();
        });


    py::class_<MatchResultHost>(m, "MatchResult")
        .def(py::init<>())
        .def("__copy__", [](const MatchResultHost& self) { return MatchResultHost(self); })
        .def_readwrite("success", &MatchResultHost::success)
        .def_readwrite("should_update", &MatchResultHost::should_update)
        .def_readwrite("score", &MatchResultHost::score)

        .def("__repr__", [](const MatchResultHost& fp) {
            std::ostringstream oss;
            oss << "<rsid_py.MatchResult "
                << "success=" << fp.success << ", "
                << "should_update=" << fp.should_update << ", "
                << "score=" << fp.score << '>';
            return oss.str();
        });
    //
    //

    ExtractedFaceprints pExtractedFaceprints;

    py::class_<FaceAuthenticator>(m, "FaceAuthenticator")
        // ctor with give device type and serial port
        .def(py::init([](DeviceType deviceType, const std::string& port) { // ctor with port to connect
            auto f = std::make_unique<FaceAuthenticator>(deviceType);
            RSID_THROW_ON_ERROR(f->Connect(SerialConfig {port.c_str()}));
            return f;
        }))

        // ctor with given serial port (F45x deviceType)
        .def(py::init([](const std::string& port) { // ctor with port to connect
            auto f = std::make_unique<FaceAuthenticator>();
            RSID_THROW_ON_ERROR(f->Connect(SerialConfig {port.c_str()}));
            return f;
        }))

        .def("__enter__", [](FaceAuthenticator& self) { return &self; })
        .def("__exit__", [](FaceAuthenticator& self, py::handle, py::handle, py::handle) { self.Disconnect(); })

        .def(
            "connect",
            [](FaceAuthenticator& self, const std::string& port) { RSID_THROW_ON_ERROR(self.Connect(SerialConfig {port.c_str()})); },
            py::call_guard<py::gil_scoped_release>())

        .def("disconnect", &FaceAuthenticator::Disconnect, py::call_guard<py::gil_scoped_release>())

        .def(
            "cancel", [](FaceAuthenticator& self) { RSID_THROW_ON_ERROR(self.Cancel()); }, py::call_guard<py::gil_scoped_release>())

        // device mode api
        .def(
            "enroll",
            [](FaceAuthenticator& self, EnrollStatusClbkFun& fn1, EnrollProgressClbkFun& fn2, EnrollHintClbkFun& fn3,
               FaceDetectedClbkFun& fn4, LandmarksDetectedClbkFun& fn5, FaceCroppedImageClbkFun& fn6, const std::string& user_id) {
                EnrollCallbackPy clbk {fn1, fn2, fn3, fn4, fn5, fn6};
                RSID_THROW_ON_ERROR(self.Enroll(clbk, user_id.c_str()));
            },
            py::arg("on_result") = EnrollStatusClbkFun {}, py::arg("on_progress") = EnrollProgressClbkFun {},
            py::arg("on_hint") = EnrollHintClbkFun {}, py::arg("on_faces") = FaceDetectedClbkFun {},
            py::arg("on_landmarks") = LandmarksDetectedClbkFun {}, py::arg("on_face_cropped_image") = FaceCroppedImageClbkFun {},
            py::arg("user_id"), py::call_guard<py::gil_scoped_release>())
        .def(
            "enroll_image",
            [](FaceAuthenticator& self, const std::string& user_id, const std::vector<unsigned char>& buffer, int width, int height) {
                return self.EnrollImage(user_id.c_str(), buffer.data(), width, height);
            },
            py::arg("user_id"), py::arg("buffer"), py::arg("width"), py::arg("height"),
            py::doc("Enroll with image. Buffer should be bgr24"), py::call_guard<py::gil_scoped_release>())
        .def(
            "extract_image_faceprints_for_enroll",
            [&](FaceAuthenticator& self, const std::vector<unsigned char>& buffer, int width, int height) {
                ExtractedFaceprints fp;
                auto status = self.EnrollImageFeatureExtraction("no_used", buffer.data(), width, height, &fp);
                if (status != RealSenseID::EnrollStatus::Success)
                    throw std::runtime_error(RealSenseID::Description(status));
                return fp.data;
            },
            py::arg("buffer"), py::arg("width"), py::arg("height"), py::doc("Enroll with image. Buffer should be bgr24"),
            py::call_guard<py::gil_scoped_release>())


        .def(
            "authenticate",
            [](FaceAuthenticator& self, AuthStatusClbkFun& fn1, AuthHintClbkFun& fn2, FaceDetectedClbkFun& fn3,
               LandmarksDetectedClbkFun& fn4, FaceDistancesClbkFun& fn5, PersonDetectedClbkFun& fn6, PersonPoseClbkFun& fn7, FaceCroppedImageClbkFun& fn8,
               BarcodeClbkFun& fn9) {
                AuthCallbackPy clbk {fn1, fn2, fn3, fn4, fn5, fn6, fn7, fn8, fn9};
                RSID_THROW_ON_ERROR(self.Authenticate(clbk));
            },
            py::arg("on_result") = AuthStatusClbkFun {}, py::arg("on_hint") = AuthHintClbkFun {},
            py::arg("on_faces") = FaceDetectedClbkFun {}, py::arg("on_landmarks") = LandmarksDetectedClbkFun {},
            py::arg("on_face_distances") = FaceDistancesClbkFun {},
            py::arg("on_person_detect") = PersonDetectedClbkFun {}, py::arg("on_person_pose") = PersonPoseClbkFun {},
            py::arg("on_face_cropped_image") = FaceCroppedImageClbkFun {}, py::arg("on_barcode") = BarcodeClbkFun {},
            py::call_guard<py::gil_scoped_release>())

        .def(
            "authenticate_loop",
            [](FaceAuthenticator& self, AuthStatusClbkFun& fn1, AuthHintClbkFun& fn2, FaceDetectedClbkFun& fn3,
               LandmarksDetectedClbkFun& fn4, FaceDistancesClbkFun& fn5, PersonDetectedClbkFun& fn6, PersonPoseClbkFun fn7, FaceCroppedImageClbkFun& fn8,
               BarcodeClbkFun& fn9) {
                AuthCallbackPy clbk {fn1, fn2, fn3, fn4, fn5, fn6, fn7, fn8, fn9};
                RSID_THROW_ON_ERROR(self.AuthenticateLoop(clbk));
            },
            py::arg("on_result") = AuthStatusClbkFun {}, py::arg("on_hint") = AuthHintClbkFun {},
            py::arg("on_faces") = FaceDetectedClbkFun {}, py::arg("on_landmarks") = LandmarksDetectedClbkFun {},
            py::arg("on_face_distances") = FaceDistancesClbkFun {},
            py::arg("on_person_detect") = PersonDetectedClbkFun {}, py::arg("on_person_pose") = PersonPoseClbkFun {},
            py::arg("on_face_cropped_image") = FaceCroppedImageClbkFun {}, py::arg("on_barcode") = BarcodeClbkFun {},
            py::call_guard<py::gil_scoped_release>())

        .def(
            "detect_persons",
            [](FaceAuthenticator& self, std::function<bool(const Persons&, unsigned int, AuthenticateStatus)> callback, bool loop) {
                auto cpp_callback = [callback](const Persons& persons, unsigned int ts, AuthenticateStatus status) -> bool {
                    py::gil_scoped_acquire acquire;
                    return callback(persons, ts, status);
                };
                RSID_THROW_ON_ERROR(self.DetectPersons(cpp_callback, loop));
            },
            py::arg("callback"), py::arg("loop") = false,
            py::call_guard<py::gil_scoped_release>())

        .def(
            "detect_poses",
            [](FaceAuthenticator& self, std::function<bool(const PersonPoses&, unsigned int, AuthenticateStatus)> callback, bool loop) {
                auto cpp_callback = [callback](const PersonPoses& poses, unsigned int ts, AuthenticateStatus status) -> bool {
                    py::gil_scoped_acquire acquire;
                    return callback(poses, ts, status);
                };
                RSID_THROW_ON_ERROR(self.DetectPoses(cpp_callback, loop));
            },
            py::arg("callback"), py::arg("loop") = false,
            py::call_guard<py::gil_scoped_release>())

        .def(
            "decode_barcodes",
            [](FaceAuthenticator& self, std::function<bool(const std::vector<std::string>&, unsigned int, AuthenticateStatus)> callback, bool loop) {
                auto cpp_callback = [callback](const std::vector<std::string>& barcodes, unsigned int ts, AuthenticateStatus status) -> bool {
                    py::gil_scoped_acquire acquire;
                    return callback(barcodes, ts, status);
                };
                RSID_THROW_ON_ERROR(self.DecodeBarcodes(cpp_callback, loop));
            },
            py::arg("callback"), py::arg("loop") = false,
            py::call_guard<py::gil_scoped_release>())

        .def(
            "detect_body_parts",
            [](FaceAuthenticator& self, std::function<bool(const Persons&, unsigned int, AuthenticateStatus)> callback, bool loop) {
                auto cpp_callback = [callback](const Persons& body_parts, unsigned int ts, AuthenticateStatus status) -> bool {
                    py::gil_scoped_acquire acquire;
                    return callback(body_parts, ts, status);
                };
                RSID_THROW_ON_ERROR(self.DetectBodyParts(cpp_callback, loop));
            },
            py::arg("callback"), py::arg("loop") = false,
            py::call_guard<py::gil_scoped_release>())

        // users api
        .def_readonly_static("MAX_USERID_LENGTH", &RealSenseID::MAX_USERID_LENGTH)
        .def(
            "remove_user",
            [](FaceAuthenticator& self, const std::string& user_id) { RSID_THROW_ON_ERROR(self.RemoveUser(user_id.c_str())); },
            py::arg("user_id"), py::call_guard<py::gil_scoped_release>())

        .def(
            "remove_all_users", [](FaceAuthenticator& self) { RSID_THROW_ON_ERROR(self.RemoveAll()); },
            py::call_guard<py::gil_scoped_release>())

        .def(
            "query_number_of_users",
            [](FaceAuthenticator& self) {
                unsigned int n_users = 0;
                RSID_THROW_ON_ERROR(self.QueryNumberOfUsers(n_users));
                return n_users;
            },
            py::call_guard<py::gil_scoped_release>())

        .def(
            "query_user_ids", [](FaceAuthenticator& self) { return query_users(self); }, py::call_guard<py::gil_scoped_release>())

        .def(
            "query_device_config",
            [](FaceAuthenticator& self) {
                DeviceConfig device_config;
                RSID_THROW_ON_ERROR(self.QueryDeviceConfig(device_config));
                return device_config;
            },
            py::call_guard<py::gil_scoped_release>())

        .def(
            "set_device_config",
            [](FaceAuthenticator& self, const DeviceConfig device_config) { RSID_THROW_ON_ERROR(self.SetDeviceConfig(device_config)); },
            py::call_guard<py::gil_scoped_release>())

        .def(
            "standby", [](FaceAuthenticator& self) { RSID_THROW_ON_ERROR(self.Standby()); }, py::call_guard<py::gil_scoped_release>())
        .def(
            "hibernate", [](FaceAuthenticator& self) { RSID_THROW_ON_ERROR(self.Hibernate()); }, py::call_guard<py::gil_scoped_release>())

        .def(
            "unlock", [](FaceAuthenticator& self) { RSID_THROW_ON_ERROR(self.Unlock()); }, py::call_guard<py::gil_scoped_release>())
        .def(
            "dump_and_mount", [](FaceAuthenticator& self) { RSID_THROW_ON_ERROR(self.DumpAndMount()); },
            py::call_guard<py::gil_scoped_release>())
        .def(
            "mount_debug", [](FaceAuthenticator& self) { RSID_THROW_ON_ERROR(self.MountDebug()); },
            py::call_guard<py::gil_scoped_release>())

        //
        // host mode api
        //

        .def(
            "extract_faceprints_for_enroll",
            [](FaceAuthenticator& self, FaceprintsEnrollStatusClbkFun& fn1, EnrollProgressClbkFun& fn2, EnrollHintClbkFun& fn3,
               FaceDetectedClbkFun& fn4, LandmarksDetectedClbkFun& fn5, FaceCroppedImageClbkFun& fn6) {
                FaceprintsEnrollCallbackPy clbk {fn1, fn2, fn3, fn4, fn5, fn6};
                RSID_THROW_ON_ERROR(self.ExtractFaceprintsForEnroll(clbk));
            },
            py::arg("on_result") = FaceprintsEnrollStatusClbkFun {}, py::arg("on_progress") = EnrollProgressClbkFun {},
            py::arg("on_hint") = EnrollHintClbkFun {}, py::arg("on_faces") = FaceDetectedClbkFun {},
            py::arg("on_landmarks") = LandmarksDetectedClbkFun {}, py::arg("on_face_cropped_image") = FaceCroppedImageClbkFun {},
            py::call_guard<py::gil_scoped_release>())


        .def(
            "extract_faceprints_for_auth",
            [](FaceAuthenticator& self, FaceprintsAuthStatusClbkFun& fn1, AuthHintClbkFun& fn2, FaceDetectedClbkFun& fn3,
               LandmarksDetectedClbkFun& fn4, FaceCroppedImageClbkFun& fn5) {
                FaceprintsAuthCallbackPy clbk {fn1, fn2, fn3, fn4, fn5};
                RSID_THROW_ON_ERROR(self.ExtractFaceprintsForAuth(clbk));
            },
            py::arg("on_result") = FaceprintsAuthStatusClbkFun {}, py::arg("on_hint") = AuthHintClbkFun {},
            py::arg("on_faces") = FaceDetectedClbkFun {}, py::arg("on_landmarks") = LandmarksDetectedClbkFun {},
            py::arg("on_face_cropped_image") = FaceCroppedImageClbkFun {}, py::call_guard<py::gil_scoped_release>())

        .def(
            "match_faceprints",
            [](FaceAuthenticator& self, ExtractedFaceprintsElement& new_faceprints, const DBFaceprintsElement& existing_faceprints,
               DBFaceprintsElement& updated_faceprints, const DeviceConfig::MatcherConfidenceLevel& confidence_level) {
                // wrap with needed classes
                MatchElement match_element;
                match_element.data = new_faceprints;

                Faceprints existing;
                existing.data = existing_faceprints;

                Faceprints updated;
                MatchResultHost match_result;

                switch (confidence_level)
                {
                case DeviceConfig::MatcherConfidenceLevel::High:
                    match_result =
                        self.MatchFaceprints(match_element, existing, updated, ThresholdsConfidenceEnum::ThresholdsConfidenceLevel_High);
                    break;
                case DeviceConfig::MatcherConfidenceLevel::Medium:
                    match_result =
                        self.MatchFaceprints(match_element, existing, updated, ThresholdsConfidenceEnum::ThresholdsConfidenceLevel_Medium);
                    break;
                case DeviceConfig::MatcherConfidenceLevel::Low:
                    match_result =
                        self.MatchFaceprints(match_element, existing, updated, ThresholdsConfidenceEnum::ThresholdsConfidenceLevel_Low);
                    break;
                }

                updated_faceprints = updated.data;
                return match_result;
            },
            py::arg("new_faceprints"), py::arg("existing_faceprints"), py::arg("updated_faceprints"),
            py::arg("confidence_level") = DeviceConfig::MatcherConfidenceLevel::High, py::call_guard<py::gil_scoped_release>());
}

// clang-format on
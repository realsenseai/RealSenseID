#include "Inference.h"
#include "Logger.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#else
#include <filesystem>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

static const char* LOG_TAG = "Inference";
static const char* FD_MODEL_FILENAME = "fd_ret_v19.onnx";
static constexpr int FD_MAX_DETECTIONS = 100;

#ifdef __ANDROID__
static AAssetManager* s_asset_manager = nullptr;
#endif

namespace
{

struct OrtState
{
    Ort::Env env;
    std::unique_ptr<Ort::Session> session;
    std::string input_name;
    std::string output_name;
};

#ifdef __ANDROID__
std::unique_ptr<Ort::Session> LoadModel(Ort::Env& env, const Ort::SessionOptions& opts)
{
    if (!s_asset_manager)
    {
        LOG_ERROR(LOG_TAG, "AssetManager not set. Call SetAssetManager() before using the pipeline.");
        throw std::runtime_error("AssetManager not set");
    }
    AAsset* asset = AAssetManager_open(s_asset_manager, FD_MODEL_FILENAME, AASSET_MODE_BUFFER);
    if (!asset)
    {
        LOG_ERROR(LOG_TAG, "Failed to open model asset: %s", FD_MODEL_FILENAME);
        throw std::runtime_error("Failed to open model asset");
    }
    const void* model_data = AAsset_getBuffer(asset);
    size_t model_size = AAsset_getLength(asset);
    auto session = std::make_unique<Ort::Session>(env, model_data, model_size, opts);
    AAsset_close(asset);
    return session;
}
#else
std::unique_ptr<Ort::Session> LoadModel(Ort::Env& env, const Ort::SessionOptions& opts)
{
    std::filesystem::path lib_dir;
#ifdef _WIN32
    HMODULE hm = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&LoadModel), &hm))
    {
        LOG_ERROR(LOG_TAG, "GetModuleHandleExA failed (error %lu)", GetLastError());
        throw std::runtime_error("Failed to locate shared library module");
    }
    char path_buf[MAX_PATH] = {0};
    if (!GetModuleFileNameA(hm, path_buf, MAX_PATH))
    {
        LOG_ERROR(LOG_TAG, "GetModuleFileNameA failed (error %lu)", GetLastError());
        throw std::runtime_error("Failed to get shared library path");
    }
    lib_dir = std::filesystem::path(path_buf).parent_path();
#else
    Dl_info info;
    if (!dladdr(reinterpret_cast<void*>(&LoadModel), &info) || !info.dli_fname)
    {
        LOG_ERROR(LOG_TAG, "dladdr failed to locate shared library path");
        throw std::runtime_error("Failed to locate shared library path");
    }
    lib_dir = std::filesystem::path(info.dli_fname).parent_path();
#endif
    auto model_path = lib_dir / FD_MODEL_FILENAME;
    return std::make_unique<Ort::Session>(env, model_path.c_str(), opts);
}
#endif

Ort::SessionOptions CreateSessionOptions()
{
    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    int num_threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) / 2);
    opts.SetIntraOpNumThreads(num_threads);
    opts.AddConfigEntry("session.intra_op.allow_spinning", "0");
    opts.AddConfigEntry("session.inter_op.allow_spinning", "0");
    LOG_INFO(LOG_TAG, "ONNX Runtime session options: %d threads", num_threads);
    return opts;
}

OrtState& GetOrt()
{
    static OrtState ort = []() {
        OrtState s;
        s.env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "RealSenseID");
        auto opts = CreateSessionOptions();
        LOG_INFO(LOG_TAG, "ONNX Runtime initialized (ver %s)", Ort::GetVersionString().c_str());
        s.session = LoadModel(s.env, opts);
        Ort::AllocatorWithDefaultOptions alloc;
        s.input_name = s.session->GetInputNameAllocated(0, alloc).get();
        s.output_name = s.session->GetOutputNameAllocated(0, alloc).get();
        return s;
    }();
    return ort;
}

} // anonymous namespace

namespace RealSenseID
{
namespace Pipeline
{

#ifdef __ANDROID__
void Inference::SetAssetManager(void* mgr)
{
    s_asset_manager = static_cast<AAssetManager*>(mgr);
    LOG_INFO(LOG_TAG, "AssetManager set");
}
#endif

std::vector<Inference::DetectedFace> Inference::RunFaceDetect(float* input_data, size_t count, int width, int height)
{
    if (input_data == nullptr || count == 0 || width <= 0 || height <= 0)
    {
        LOG_ERROR(LOG_TAG, "RunFaceDetect: invalid input parameters");
        return {};
    }

    auto& ort = GetOrt();
    std::vector<DetectedFace> detections;

    std::array<int64_t, 4> input_shape = {1, height, width, 3};
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto input_tensor = Ort::Value::CreateTensor<float>(memory_info, input_data, count, input_shape.data(), input_shape.size());

    const char* input_names[] = {ort.input_name.c_str()};
    const char* output_names[] = {ort.output_name.c_str()};

    auto output_tensors = ort.session->Run(Ort::RunOptions {nullptr}, input_names, &input_tensor, 1, output_names, 1);

    const float* output = output_tensors[0].GetTensorData<float>();
    auto output_info = output_tensors[0].GetTensorTypeAndShapeInfo();
    auto output_shape = output_info.GetShape();
    if (output_shape.size() != 2 || output_shape[1] != 5)
    {
        LOG_ERROR(LOG_TAG, "RunFaceDetect: unexpected output shape");
        return detections;
    }

    const int num = std::min(static_cast<int>(output_shape[0]), FD_MAX_DETECTIONS);
    for (int i = 0; i < num * 5; i += 5)
    {
        detections.push_back({output[i], output[i + 1], output[i + 2], output[i + 3], output[i + 4]});
    }

    return detections;
}

} // namespace Pipeline
} // namespace RealSenseID

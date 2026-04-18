// paddle_inference_api.h
// 最小化 Paddle Inference C++ API 声明（用于编译验证）
// 生产环境请替换为从官方下载的完整 Paddle Inference 库
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace paddle_infer {

// 数据类型
enum DataType {
    FLOAT32,
    INT32,
    INT64,
    UINT8,
    INT8
};

// Tensor —— 输入/输出句柄
class Tensor {
public:
    virtual ~Tensor() = default;
    virtual void Reshape(const std::vector<int>& shape) = 0;
    virtual void CopyFromCpu(const float* data) = 0;
    virtual void CopyFromCpu(const int32_t* data) = 0;
    virtual void CopyFromCpu(const int64_t* data) = 0;
    virtual void CopyToCpu(float* data) const = 0;
    virtual void CopyToCpu(int32_t* data) const = 0;
    virtual void CopyToCpu(int64_t* data) const = 0;
    virtual std::vector<int> shape() const = 0;
    virtual DataType type() const = 0;
};

// Predictor —— 推理引擎
class Predictor {
public:
    virtual ~Predictor() = default;
    virtual std::vector<std::string> GetInputNames() = 0;
    virtual std::vector<std::string> GetOutputNames() = 0;
    virtual std::unique_ptr<Tensor> GetInputHandle(const std::string& name) = 0;
    virtual std::unique_ptr<Tensor> GetOutputHandle(const std::string& name) = 0;
    virtual bool Run() = 0;
};

// Config —— 模型配置
class Config {
public:
    Config() = default;
    ~Config() = default;

    void SetModel(const std::string& model_file, const std::string& params_file) {
        model_file_ = model_file;
        params_file_ = params_file;
    }
    void DisableGpu() { use_gpu_ = false; }
    void EnableMKLDNN() { use_mkldnn_ = true; }
    void SetCpuMathLibraryNumThreads(int num) { cpu_threads_ = num; }
    void SwitchIrOptim(bool enable = true) { ir_optim_ = enable; }
    void DisableGlogInfo() { glog_info_ = false; }

    const std::string& model_file() const { return model_file_; }
    const std::string& params_file() const { return params_file_; }

private:
    std::string model_file_;
    std::string params_file_;
    bool use_gpu_ = false;
    bool use_mkldnn_ = false;
    int cpu_threads_ = 1;
    bool ir_optim_ = true;
    bool glog_info_ = true;
};

// 工厂方法
std::shared_ptr<Predictor> CreatePredictor(const Config& config);

} // namespace paddle_infer

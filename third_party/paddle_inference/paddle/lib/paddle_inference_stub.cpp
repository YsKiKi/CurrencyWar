// paddle_inference_stub.cpp
// 最小化存根实现（仅用于编译验证，不提供实际推理功能）
// 生产环境请替换为官方 Paddle Inference 库

#include "paddle_inference_api.h"
#include <stdexcept>

namespace paddle_infer {

class StubTensor : public Tensor {
public:
    void Reshape(const std::vector<int>& shape) override { shape_ = shape; }
    void CopyFromCpu(const float*) override {}
    void CopyFromCpu(const int32_t*) override {}
    void CopyFromCpu(const int64_t*) override {}
    void CopyToCpu(float*) const override {}
    void CopyToCpu(int32_t*) const override {}
    void CopyToCpu(int64_t*) const override {}
    std::vector<int> shape() const override { return shape_; }
    DataType type() const override { return FLOAT32; }
private:
    std::vector<int> shape_;
};

class StubPredictor : public Predictor {
public:
    std::vector<std::string> GetInputNames() override { return {"x"}; }
    std::vector<std::string> GetOutputNames() override { return {"out"}; }
    std::unique_ptr<Tensor> GetInputHandle(const std::string&) override {
        return std::make_unique<StubTensor>();
    }
    std::unique_ptr<Tensor> GetOutputHandle(const std::string&) override {
        return std::make_unique<StubTensor>();
    }
    bool Run() override { return true; }
};

std::shared_ptr<Predictor> CreatePredictor(const Config&) {
    return std::make_shared<StubPredictor>();
}

} // namespace paddle_infer

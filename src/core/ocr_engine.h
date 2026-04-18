#pragma once
// ocr_engine.h — 基于 PaddleOCR (Paddle Inference C++) 的文字识别模块

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <opencv2/core.hpp>

struct OCRResult {
    std::string text;
    float confidence = 0.0f;
    // box: 四顶点 [[x1,y1],[x2,y2],[x3,y3],[x4,y4]]（顺时针）
    std::vector<std::pair<int, int>> box;
    std::string matched_strategy; // 附加字段（Bot 使用）

    std::pair<int, int> center() const;
};

class OCREngine {
public:
    /// @param model_dir  PaddleOCR 模型根目录，期望子目录:
    ///   ch_PP-OCRv4_det_infer/   (文本检测)
    ///   ch_PP-OCRv4_rec_infer/   (文本识别)
    ///   ppocr_keys_v1.txt        (字典)
    /// @param lang  "ch" 中文 | "en" 英文（预留，当前均使用中文模型）
    explicit OCREngine(const std::string& model_dir = "res/ocr",
                       const std::string& lang = "ch");
    ~OCREngine();

    // 对整张图像进行 OCR 识别
    std::vector<OCRResult> recognize(const cv::Mat& image) const;

    // 对图像的指定矩形区域进行 OCR 识别（坐标已换算回原图）
    std::vector<OCRResult> recognize_region(const cv::Mat& image,
                                            int x, int y, int w, int h) const;

    // 查找包含 target 的第一个文字区域
    std::optional<OCRResult> find_text(const cv::Mat& image,
                                       const std::string& target,
                                       float confidence_threshold = 0.5f,
                                       bool exact = false) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

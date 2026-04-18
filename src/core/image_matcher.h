#pragma once
// image_matcher.h — 基于 OpenCV 的图像模板匹配模块

#include <string>
#include <vector>
#include <tuple>
#include <optional>
#include <opencv2/core.hpp>

struct MatchResult {
    int center_x = 0, center_y = 0;
    float confidence = 0.0f;
    int left = 0, top = 0, width = 0, height = 0;

    std::pair<int,int> center() const { return {center_x, center_y}; }
    // (left, top, right, bottom)
    std::tuple<int,int,int,int> rect() const {
        return {left, top, left + width, top + height};
    }
};

class ImageMatcher {
public:
    explicit ImageMatcher(float threshold = 0.8f);

    // 在截图中查找模板，返回置信度最高的一个结果
    std::optional<MatchResult> find(const cv::Mat& screenshot,
                                     const std::string& template_path,
                                     float threshold = -1.0f) const;

    std::optional<MatchResult> find(const cv::Mat& screenshot,
                                     const cv::Mat& tpl,
                                     float threshold = -1.0f) const;

    // 查找所有匹配位置
    std::vector<MatchResult> find_all(const cv::Mat& screenshot,
                                       const cv::Mat& tpl,
                                       float threshold = -1.0f,
                                       float nms_overlap = 0.4f) const;

    // 加载模板图像
    static cv::Mat load_template(const std::string& path);

private:
    float threshold_;

    static std::vector<MatchResult> nms(std::vector<MatchResult>& matches,
                                         int tpl_w, int tpl_h, float overlap);
};

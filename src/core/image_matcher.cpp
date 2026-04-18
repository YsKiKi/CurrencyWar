#include "core/image_matcher.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <cstdio>
#include <Windows.h>

ImageMatcher::ImageMatcher(float threshold) : threshold_(threshold) {}

// 用 Win32 宽字符 API 读取文件，绕过 cv::imread 的 ANSI 限制
static cv::Mat imread_utf8(const std::string& path, int flags) {
    // UTF-8 → wchar_t
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return {};
    std::vector<wchar_t> wpath(wlen);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    FILE* f = _wfopen(wpath.data(), L"rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uchar> buf(sz);
    fread(buf.data(), 1, sz, f);
    fclose(f);
    return cv::imdecode(buf, flags);
}

cv::Mat ImageMatcher::load_template(const std::string& path) {
    cv::Mat tpl = imread_utf8(path, cv::IMREAD_COLOR);
    if (tpl.empty()) {
        throw std::runtime_error("无法加载模板: " + path);
    }
    return tpl;
}

std::optional<MatchResult> ImageMatcher::find(const cv::Mat& screenshot,
                                               const std::string& template_path,
                                               float threshold) const {
    cv::Mat tpl = load_template(template_path);
    return find(screenshot, tpl, threshold);
}

std::optional<MatchResult> ImageMatcher::find(const cv::Mat& screenshot,
                                               const cv::Mat& tpl,
                                               float threshold) const {
    if (screenshot.empty() || tpl.empty()) return std::nullopt;

    float thr = (threshold >= 0) ? threshold : threshold_;

    cv::Mat src_gray, tpl_gray;
    cv::cvtColor(screenshot, src_gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(tpl, tpl_gray, cv::COLOR_BGR2GRAY);

    int h = tpl_gray.rows, w = tpl_gray.cols;
    if (src_gray.rows < h || src_gray.cols < w) return std::nullopt;

    cv::Mat result;
    cv::matchTemplate(src_gray, tpl_gray, result, cv::TM_CCOEFF_NORMED);

    double max_val;
    cv::Point max_loc;
    cv::minMaxLoc(result, nullptr, &max_val, nullptr, &max_loc);

    if (max_val < thr) return std::nullopt;

    MatchResult mr;
    mr.center_x = max_loc.x + w / 2;
    mr.center_y = max_loc.y + h / 2;
    mr.confidence = static_cast<float>(max_val);
    mr.left = max_loc.x;
    mr.top = max_loc.y;
    mr.width = w;
    mr.height = h;
    return mr;
}

std::vector<MatchResult> ImageMatcher::find_all(const cv::Mat& screenshot,
                                                  const cv::Mat& tpl,
                                                  float threshold,
                                                  float nms_overlap) const {
    if (screenshot.empty() || tpl.empty()) return {};

    float thr = (threshold >= 0) ? threshold : threshold_;

    cv::Mat src_gray, tpl_gray;
    cv::cvtColor(screenshot, src_gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(tpl, tpl_gray, cv::COLOR_BGR2GRAY);

    int h = tpl_gray.rows, w = tpl_gray.cols;
    if (src_gray.rows < h || src_gray.cols < w) return {};

    cv::Mat result;
    cv::matchTemplate(src_gray, tpl_gray, result, cv::TM_CCOEFF_NORMED);

    std::vector<MatchResult> candidates;
    for (int y = 0; y < result.rows; ++y) {
        for (int x = 0; x < result.cols; ++x) {
            float val = result.at<float>(y, x);
            if (val >= thr) {
                MatchResult mr;
                mr.center_x = x + w / 2;
                mr.center_y = y + h / 2;
                mr.confidence = val;
                mr.left = x;
                mr.top = y;
                mr.width = w;
                mr.height = h;
                candidates.push_back(mr);
            }
        }
    }

    return nms(candidates, w, h, nms_overlap);
}

std::vector<MatchResult> ImageMatcher::nms(std::vector<MatchResult>& matches,
                                            int tpl_w, int tpl_h, float overlap) {
    if (matches.empty()) return {};

    std::sort(matches.begin(), matches.end(),
              [](const MatchResult& a, const MatchResult& b) {
                  return a.confidence > b.confidence;
              });

    float min_dx = tpl_w * (1.0f - overlap);
    float min_dy = tpl_h * (1.0f - overlap);

    std::vector<MatchResult> kept;
    for (auto& m : matches) {
        bool duplicate = false;
        for (auto& k : kept) {
            if (std::abs(m.center_x - k.center_x) < min_dx &&
                std::abs(m.center_y - k.center_y) < min_dy) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) kept.push_back(m);
    }
    return kept;
}

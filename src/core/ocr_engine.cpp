#include "core/ocr_engine.h"
#include <paddle_inference_api.h>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <filesystem>

// ======================================================================
// OCRResult
// ======================================================================

std::pair<int, int> OCRResult::center() const {
    if (box.empty()) return {0, 0};
    int cx = 0, cy = 0;
    for (auto& [px, py] : box) { cx += px; cy += py; }
    return {cx / static_cast<int>(box.size()), cy / static_cast<int>(box.size())};
}

// ======================================================================
// 内部辅助
// ======================================================================

// 检测预处理参数
static constexpr int    DET_MAX_SIDE_LEN   = 960;
static constexpr float  DET_MEAN[]         = {0.485f, 0.456f, 0.406f};
static constexpr float  DET_STD[]          = {0.229f, 0.224f, 0.225f};
static constexpr float  DET_SCALE          = 1.0f / 255.0f;

// DB 后处理参数
static constexpr float  DB_THRESH          = 0.3f;
static constexpr float  DB_BOX_THRESH      = 0.6f;
static constexpr float  DB_UNCLIP_RATIO    = 2.0f;
static constexpr int    DB_MAX_CANDIDATES  = 1000;
static constexpr int    DB_MIN_SIZE        = 3;

// 识别预处理参数
static constexpr int    REC_IMG_H          = 48;
static constexpr int    REC_IMG_MAX_W      = 320;

// ── 辅助函数 ──

static float distance(const cv::Point2f& a, const cv::Point2f& b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

/// 将 4 个点排序为: 左上、右上、右下、左下
static std::vector<cv::Point2f> order_points(std::vector<cv::Point2f> pts) {
    // 按 x+y 排序找 左上(最小) 和 右下(最大)
    std::sort(pts.begin(), pts.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return (a.x + a.y) < (b.x + b.y);
    });
    cv::Point2f tl = pts[0], br = pts[3];
    // 按 y-x 排序找 右上(最小) 和 左下(最大)
    std::sort(pts.begin(), pts.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return (a.y - a.x) < (b.y - b.x);
    });
    cv::Point2f tr = pts[0], bl = pts[3];
    return {tl, tr, br, bl};
}

/// 计算 contour 在概率图上的平均得分
static float box_score_fast(const cv::Mat& prob_map,
                            const std::vector<cv::Point>& contour) {
    int h = prob_map.rows, w = prob_map.cols;
    cv::Rect bound = cv::boundingRect(contour);
    int xmin = std::max(bound.x, 0);
    int ymin = std::max(bound.y, 0);
    int xmax = std::min(bound.x + bound.width, w - 1);
    int ymax = std::min(bound.y + bound.height, h - 1);

    cv::Mat mask = cv::Mat::zeros(ymax - ymin + 1, xmax - xmin + 1, CV_8UC1);
    std::vector<cv::Point> shifted;
    for (auto& p : contour)
        shifted.push_back({p.x - xmin, p.y - ymin});
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{shifted}, 255);

    cv::Mat crop = prob_map(cv::Rect(xmin, ymin, xmax - xmin + 1, ymax - ymin + 1));
    return static_cast<float>(cv::mean(crop, mask)[0]);
}

/// unclip：对多边形每条边沿法线向外偏移 dist = area * ratio / perimeter
/// 等效于 pyclipper.ClipperOffset (JT_SQUARE) 的简化实现
static std::vector<cv::Point2f> unclip(const std::vector<cv::Point2f>& poly, float ratio) {
    double area = std::abs(cv::contourArea(poly));
    double length = cv::arcLength(poly, true);
    if (length < 1e-6) return poly;
    double dist = area * ratio / length;

    int n = static_cast<int>(poly.size());
    if (n < 3) return poly;

    // 判断绕行方向 (正值=CW in screen coords / y-down)
    double cross_sum = 0;
    for (int i = 0; i < n; i++) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % n];
        cross_sum += static_cast<double>(b.x - a.x) * (b.y + a.y);
    }

    // 对每条边计算向外偏移后的直线: a*x + b*y = c
    struct Line { double a, b, c; };
    std::vector<Line> lines;
    for (int i = 0; i < n; i++) {
        const auto& p1 = poly[i];
        const auto& p2 = poly[(i + 1) % n];
        double dx = p2.x - p1.x;
        double dy = p2.y - p1.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) {
            lines.push_back({0.0, 1.0, static_cast<double>(p1.y)});
            continue;
        }
        // 外法线 (screen coords: y-down)
        double nx, ny;
        if (cross_sum > 0) { // CW (screen)
            nx = -dy / len;
            ny =  dx / len;
        } else { // CCW (screen)
            nx =  dy / len;
            ny = -dx / len;
        }
        // 偏移后直线: nx*x + ny*y = nx*p1.x + ny*p1.y + dist
        double c = nx * p1.x + ny * p1.y + dist;
        lines.push_back({nx, ny, c});
    }

    // 相邻偏移线求交 → 新顶点
    std::vector<cv::Point2f> result;
    for (int i = 0; i < n; i++) {
        const auto& l1 = lines[(i - 1 + n) % n]; // 到达顶点 i 的边
        const auto& l2 = lines[i];                // 离开顶点 i 的边
        double det = l1.a * l2.b - l2.a * l1.b;
        if (std::abs(det) < 1e-9) {
            result.push_back(poly[i]);
            continue;
        }
        double x = (l1.c * l2.b - l2.c * l1.b) / det;
        double y = (l1.a * l2.c - l2.a * l1.c) / det;
        result.push_back({static_cast<float>(x), static_cast<float>(y)});
    }
    return result;
}

/// 从原图中透视变换裁剪文本行
static cv::Mat get_rotate_crop_image(const cv::Mat& src,
                                     const std::vector<cv::Point2f>& box) {
    auto pts = order_points(box);
    float w = std::max(distance(pts[0], pts[1]), distance(pts[2], pts[3]));
    float h = std::max(distance(pts[0], pts[3]), distance(pts[1], pts[2]));
    int iw = std::max(1, static_cast<int>(w));
    int ih = std::max(1, static_cast<int>(h));

    std::vector<cv::Point2f> dst = {{0, 0}, {(float)iw, 0}, {(float)iw, (float)ih}, {0, (float)ih}};
    cv::Mat M = cv::getPerspectiveTransform(pts, dst);
    cv::Mat crop;
    cv::warpPerspective(src, crop, M, cv::Size(iw, ih), cv::INTER_LINEAR, cv::BORDER_REPLICATE);

    // 如果高度远大于宽度，旋转 90°
    if (crop.rows >= crop.cols * 1.5) {
        cv::rotate(crop, crop, cv::ROTATE_90_CLOCKWISE);
    }
    return crop;
}

// ======================================================================
// OCREngine::Impl
// ======================================================================

struct OCREngine::Impl {
    std::shared_ptr<paddle_infer::Predictor> det_predictor;
    std::shared_ptr<paddle_infer::Predictor> rec_predictor;
    std::vector<std::string> dict; // 字符字典

    // ── 构造 ──

    void init(const std::string& model_dir) {
        std::string det_dir = model_dir + "/ch_PP-OCRv4_det_infer";
        std::string rec_dir = model_dir + "/ch_PP-OCRv4_rec_infer";
        std::string dict_path = model_dir + "/ppocr_keys_v1.txt";

        det_predictor = create_predictor(det_dir);
        rec_predictor = create_predictor(rec_dir);
        load_dict(dict_path);

        std::cout << "[ocr] PaddleOCR 初始化完成 (det=" << det_dir
                  << ", rec=" << rec_dir << ", dict=" << dict.size() << " chars)" << std::endl;
    }

    std::shared_ptr<paddle_infer::Predictor> create_predictor(const std::string& model_dir) {
        std::string model_file  = model_dir + "/inference.pdmodel";
        std::string params_file = model_dir + "/inference.pdiparams";

        if (!std::filesystem::exists(model_file)) {
            throw std::runtime_error("模型文件不存在: " + model_file);
        }

        paddle_infer::Config config;
        config.SetModel(model_file, params_file);
        config.DisableGpu();
        config.DisableMKLDNN();                 // 显式禁用 OneDNN，避免动态形状 primitive 缓存冲突
        config.SwitchIrOptim(false);            // 禁用 IR 优化 pass，防止自动启用 OneDNN 算子
        config.SetCpuMathLibraryNumThreads(1);  // 避免 MKL 多线程 primitive 冲突
        config.EnableMemoryOptim();             // 内存复用
        // 静默日志
        config.DisableGlogInfo();

        auto predictor = paddle_infer::CreatePredictor(config);
        if (!predictor) {
            throw std::runtime_error("无法创建 Paddle 预测器: " + model_dir);
        }
        return predictor;
    }

    void load_dict(const std::string& path) {
        dict.clear();
        dict.push_back(""); // index 0 = CTC blank
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("字典文件不存在: " + path);
        }
        std::ifstream ifs(path, std::ios::in);
        std::string line;
        while (std::getline(ifs, line)) {
            // 去除行尾 \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            dict.push_back(line);
        }
        dict.push_back(" "); // 末尾空格 token
        std::cout << "[ocr] 加载字典: " << (dict.size() - 1) << " 个字符" << std::endl;
    }

    // ── 检测 ──

    struct TextBox {
        std::vector<cv::Point2f> points; // 4 corners (ordered)
        float score;
    };

    std::vector<TextBox> detect(const cv::Mat& image) {
        // 1. 预处理
        float ratio_h, ratio_w;
        cv::Mat input_blob = det_preprocess(image, ratio_h, ratio_w);
        int inp_h = input_blob.rows, inp_w = input_blob.cols;

        // 转 CHW
        std::vector<float> input_data(3 * inp_h * inp_w);
        for (int c = 0; c < 3; c++) {
            for (int i = 0; i < inp_h; i++) {
                for (int j = 0; j < inp_w; j++) {
                    input_data[c * inp_h * inp_w + i * inp_w + j] =
                        input_blob.at<cv::Vec3f>(i, j)[c];
                }
            }
        }

        // 2. 推理
        auto input_names = det_predictor->GetInputNames();
        auto input_handle = det_predictor->GetInputHandle(input_names[0]);
        input_handle->Reshape({1, 3, inp_h, inp_w});
        input_handle->CopyFromCpu(input_data.data());

        if (!det_predictor->Run()) {
            throw std::runtime_error("检测模型推理失败 (det_predictor->Run() 返回 false)");
        }

        auto output_names = det_predictor->GetOutputNames();
        auto output_handle = det_predictor->GetOutputHandle(output_names[0]);
        auto out_shape = output_handle->shape();
        if (out_shape.size() < 4) {
            throw std::runtime_error("检测模型输出维度不足: 期望>=4, 实际=" + std::to_string(out_shape.size()));
        }
        int out_h = out_shape[2], out_w = out_shape[3];
        std::vector<float> output_data(out_h * out_w);
        output_handle->CopyToCpu(output_data.data());

        // 3. DB 后处理
        cv::Mat prob_map(out_h, out_w, CV_32FC1, output_data.data());
        return db_postprocess(prob_map, image.rows, image.cols, ratio_h, ratio_w);
    }

    cv::Mat det_preprocess(const cv::Mat& image, float& ratio_h, float& ratio_w) {
        cv::Mat rgb;
        cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
        rgb.convertTo(rgb, CV_32FC3, DET_SCALE);

        // 减均值除标准差
        std::vector<cv::Mat> channels(3);
        cv::split(rgb, channels);
        for (int c = 0; c < 3; c++) {
            channels[c] = (channels[c] - DET_MEAN[c]) / DET_STD[c];
        }
        cv::Mat norm;
        cv::merge(channels, norm);

        // resize: 限制最大边 + pad to multiple of 32
        int h = norm.rows, w = norm.cols;
        float ratio = 1.0f;
        int max_side = std::max(h, w);
        if (max_side > DET_MAX_SIDE_LEN) {
            ratio = static_cast<float>(DET_MAX_SIDE_LEN) / max_side;
        }
        int new_h = static_cast<int>(h * ratio);
        int new_w = static_cast<int>(w * ratio);
        // 确保为 32 的倍数
        new_h = std::max(32, ((new_h + 31) / 32) * 32);
        new_w = std::max(32, ((new_w + 31) / 32) * 32);

        ratio_h = static_cast<float>(new_h) / h;
        ratio_w = static_cast<float>(new_w) / w;

        cv::Mat resized;
        cv::resize(norm, resized, cv::Size(new_w, new_h));
        return resized;
    }

    std::vector<TextBox> db_postprocess(const cv::Mat& prob_map,
                                        int src_h, int src_w,
                                        float ratio_h, float ratio_w) {
        // 二值化
        cv::Mat binary;
        cv::threshold(prob_map, binary, DB_THRESH, 1.0, cv::THRESH_BINARY);
        cv::Mat binary_u8;
        binary.convertTo(binary_u8, CV_8UC1, 255);

        // 膨胀（近似 unclip 效果）
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
        cv::dilate(binary_u8, binary_u8, kernel);

        // 找轮廓
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary_u8, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

        std::vector<TextBox> boxes;
        int num = std::min(static_cast<int>(contours.size()), DB_MAX_CANDIDATES);

        for (int i = 0; i < num; i++) {
            auto& contour = contours[i];
            if (contour.size() < 4) continue;

            // 最小面积矩形
            cv::RotatedRect rr = cv::minAreaRect(contour);
            if (std::min(rr.size.width, rr.size.height) < DB_MIN_SIZE) continue;

            // 打分
            float score = box_score_fast(prob_map, contour);
            if (score < DB_BOX_THRESH) continue;

            // 获取 4 顶点
            cv::Point2f pts4[4];
            rr.points(pts4);
            std::vector<cv::Point2f> poly(pts4, pts4 + 4);

            // unclip
            poly = ::unclip(poly, DB_UNCLIP_RATIO);

            // 重新求最小矩形
            cv::RotatedRect rr2 = cv::minAreaRect(poly);
            if (std::min(rr2.size.width, rr2.size.height) < DB_MIN_SIZE + 2) continue;

            cv::Point2f pts2[4];
            rr2.points(pts2);

            // 映射回原图坐标
            TextBox tb;
            tb.score = score;
            for (int j = 0; j < 4; j++) {
                float x = std::max(0.0f, std::min(pts2[j].x / ratio_w, (float)(src_w - 1)));
                float y = std::max(0.0f, std::min(pts2[j].y / ratio_h, (float)(src_h - 1)));
                tb.points.push_back({x, y});
            }
            tb.points = order_points(tb.points);
            boxes.push_back(std::move(tb));
        }

        // 排序：从上到下、从左到右
        sort_boxes(boxes);
        return boxes;
    }

    void sort_boxes(std::vector<TextBox>& boxes) {
        std::sort(boxes.begin(), boxes.end(), [](const TextBox& a, const TextBox& b) {
            // 以左上角 y 坐标为主排序，x 为次排序
            float ay = a.points.empty() ? 0 : a.points[0].y;
            float by = b.points.empty() ? 0 : b.points[0].y;
            float ax = a.points.empty() ? 0 : a.points[0].x;
            float bx = b.points.empty() ? 0 : b.points[0].x;
            if (std::abs(ay - by) > 10) return ay < by;
            return ax < bx;
        });
    }

    // ── 识别 ──

    struct RecResult {
        std::string text;
        float confidence;
    };

    RecResult recognize_crop(const cv::Mat& crop) {
        // 1. 预处理: resize to height=48, keep aspect ratio
        int h = crop.rows, w = crop.cols;
        float aspect = static_cast<float>(w) / h;
        int target_w = std::min(static_cast<int>(REC_IMG_H * aspect), REC_IMG_MAX_W);
        target_w = std::max(target_w, 1);

        cv::Mat resized;
        cv::resize(crop, resized, cv::Size(target_w, REC_IMG_H));
        resized.convertTo(resized, CV_32FC3, 1.0f / 255.0f);

        // 归一化: (x - 0.5) / 0.5 = x * 2 - 1
        resized = resized * 2.0f - 1.0f;

        // pad to REC_IMG_MAX_W
        cv::Mat padded = cv::Mat::zeros(REC_IMG_H, REC_IMG_MAX_W, CV_32FC3);
        resized.copyTo(padded(cv::Rect(0, 0, target_w, REC_IMG_H)));

        // 转 CHW, BGR→RGB 后 CHW
        cv::Mat rgb;
        cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);

        int ch = 3, ph = REC_IMG_H, pw = REC_IMG_MAX_W;
        std::vector<float> input_data(ch * ph * pw);
        for (int c = 0; c < ch; c++) {
            for (int i = 0; i < ph; i++) {
                for (int j = 0; j < pw; j++) {
                    input_data[c * ph * pw + i * pw + j] = rgb.at<cv::Vec3f>(i, j)[c];
                }
            }
        }

        // 2. 推理
        auto input_names = rec_predictor->GetInputNames();
        auto input_handle = rec_predictor->GetInputHandle(input_names[0]);
        input_handle->Reshape({1, ch, ph, pw});
        input_handle->CopyFromCpu(input_data.data());

        if (!rec_predictor->Run()) {
            throw std::runtime_error("识别模型推理失败 (rec_predictor->Run() 返回 false)");
        }

        auto output_names = rec_predictor->GetOutputNames();
        auto output_handle = rec_predictor->GetOutputHandle(output_names[0]);
        auto out_shape = output_handle->shape();
        // out_shape: [1, T, vocab_size]
        if (out_shape.size() < 3) {
            throw std::runtime_error("识别模型输出维度不足: 期望>=3, 实际=" + std::to_string(out_shape.size()));
        }
        int T = out_shape[1];
        int vocab_size = out_shape[2];

        std::vector<float> output_data(T * vocab_size);
        output_handle->CopyToCpu(output_data.data());

        // 3. CTC 解码
        return ctc_decode(output_data.data(), T, vocab_size);
    }

    RecResult ctc_decode(const float* data, int T, int vocab_size) {
        std::string text;
        float total_conf = 0.0f;
        int char_count = 0;
        int prev_idx = 0; // 0 = blank

        for (int t = 0; t < T; t++) {
            const float* row = data + t * vocab_size;
            // argmax
            int max_idx = 0;
            float max_val = row[0];
            for (int v = 1; v < vocab_size; v++) {
                if (row[v] > max_val) {
                    max_val = row[v];
                    max_idx = v;
                }
            }
            // CTC: 跳过 blank 和重复
            if (max_idx != 0 && max_idx != prev_idx) {
                if (max_idx < static_cast<int>(dict.size())) {
                    text += dict[max_idx];
                }
                total_conf += max_val;
                char_count++;
            }
            prev_idx = max_idx;
        }

        float avg_conf = (char_count > 0) ? (total_conf / char_count) : 0.0f;
        return {text, avg_conf};
    }

    // ── 完整管线 ──

    std::vector<OCRResult> run_ocr(const cv::Mat& image) {
        if (image.empty()) return {};

        // 重试机制: OneDNN primitive 错误可能是瞬态的
        for (int attempt = 0; attempt < 2; ++attempt) {
            try {
                // 检测
                auto text_boxes = detect(image);
                if (text_boxes.empty()) return {};

                // 逐框识别
                std::vector<OCRResult> results;
                for (auto& tb : text_boxes) {
                    cv::Mat crop = get_rotate_crop_image(image, tb.points);
                    if (crop.empty() || crop.rows < 2 || crop.cols < 2) continue;

                    auto rec = recognize_crop(crop);
                    if (rec.text.empty()) continue;

                    OCRResult r;
                    r.text = rec.text;
                    r.confidence = rec.confidence;
                    for (auto& p : tb.points) {
                        r.box.push_back({static_cast<int>(p.x), static_cast<int>(p.y)});
                    }
                    results.push_back(std::move(r));
                }
                return results;
            } catch (const std::exception& e) {
                std::cerr << "[ocr] 推理异常 (尝试 " << (attempt + 1) << "/2): "
                          << e.what() << std::endl;
                if (attempt == 1) return {};  // 重试后仍失败，返回空
            }
        }
        return {};
    }
};

// ======================================================================
// OCREngine 公共接口
// ======================================================================

OCREngine::OCREngine(const std::string& model_dir, const std::string& /*lang*/)
    : impl_(std::make_unique<Impl>())
{
    impl_->init(model_dir);
}

OCREngine::~OCREngine() = default;

std::vector<OCRResult> OCREngine::recognize(const cv::Mat& image) const {
    return impl_->run_ocr(image);
}

std::vector<OCRResult> OCREngine::recognize_region(const cv::Mat& image,
                                                    int x, int y, int w, int h) const {
    if (image.empty()) return {};
    int rx = std::max(0, x);
    int ry = std::max(0, y);
    int rw = std::min(w, image.cols - rx);
    int rh = std::min(h, image.rows - ry);
    if (rw <= 0 || rh <= 0) return {};

    cv::Mat crop = image(cv::Rect(rx, ry, rw, rh)).clone();

    // 增强画质: 2x 上采样 + 锐化，提升低分辨率截图的识别率
    cv::Mat enhanced;
    cv::resize(crop, enhanced, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);
    // USM 锐化: 高斯模糊后与原图加权混合
    cv::Mat blurred;
    cv::GaussianBlur(enhanced, blurred, cv::Size(0, 0), 2.0);
    cv::addWeighted(enhanced, 1.5, blurred, -0.5, 0, enhanced);

    auto results = impl_->run_ocr(enhanced);
    // 坐标缩放回原始裁剪尺寸，再偏移回原图
    for (auto& r : results) {
        for (auto& [px, py] : r.box) {
            px = static_cast<int>(px / 2.0) + rx;
            py = static_cast<int>(py / 2.0) + ry;
        }
    }
    return results;
}

std::optional<OCRResult> OCREngine::find_text(const cv::Mat& image,
                                               const std::string& target,
                                               float confidence_threshold,
                                               bool exact) const {
    auto results = recognize(image);
    for (auto& item : results) {
        if (item.confidence < confidence_threshold) continue;
        bool match = exact
            ? (item.text == target)
            : (item.text.find(target) != std::string::npos);
        if (match) return item;
    }
    return std::nullopt;
}

#pragma once
// bot.h — 货币战争自动化主控

#include "core/config.h"
#include "core/window_controller.h"
#include "core/ocr_engine.h"
#include "core/image_matcher.h"
#include "core/overlay.h"

#include <atomic>
#include <string>
#include <vector>
#include <set>
#include <optional>
#include <functional>

class CurrencyWarBot {
public:
    CurrencyWarBot(WindowController& window, OCREngine& ocr,
                   ImageMatcher& matcher, const AppConfig& config = {});

    // 启动阻塞自动化主循环
    void run();

    // 停止
    void stop();

    // 停止回调，热键触发后的额外通知
    std::function<void()> on_stopped;

private:
    bool stopped() const;

    // 截图
    cv::Mat shot();

    // ── 覆盖层辅助 ──
    void init_overlay();
    void reposition_overlay();
    void olog(const std::string& msg);
    void ostep(const std::string& step);
    void mark_ocr(const std::vector<OCRResult>& results,
                  const std::set<std::string>& keywords = {});
    void mark_match(const std::optional<MatchResult>& match, const std::string& label = "");
    void clear_marks();

    // ── 底层工具 ──
    bool wait_and_click_text(const std::string& text,
                             double timeout = 90.0, double post_delay = 0.6, int retries = 3);
    bool wait_and_click_image(const std::string& path,
                              double timeout = 90.0, double post_delay = 0.6, int retries = 3);
    bool wait_for_image(const std::string& path, double timeout = 90.0);

    // ── OCR 扫描 ──
    std::vector<OCRResult> scan_env_region();
    std::vector<OCRResult> scan_debuff_region();

    std::optional<std::string> match_strategy(const std::string& text) const;
    std::optional<std::string> match_debuff(const std::string& text) const;

    struct ValidatedResult {
        OCRResult ocr;
        std::string matched_strategy;
        std::pair<int,int> center;
    };

    std::vector<ValidatedResult> validate_env_results(const std::vector<OCRResult>& results) const;
    std::vector<std::string> validate_debuff_results(const std::vector<OCRResult>& results) const;

    std::vector<ValidatedResult> stable_scan_env(int expected_count = 3);
    std::vector<std::string> stable_scan_debuffs();

    std::optional<ValidatedResult> find_target_env(const std::vector<ValidatedResult>& results) const;
    std::vector<OCRResult> wait_for_env_screen(double timeout = 90.0);

    // ── 阶段 ──
    bool phase1();
    std::string phase2();
    void phase3_exit();

    // 成员
    WindowController& window_;
    OCREngine& ocr_;
    ImageMatcher& matcher_;
    AppConfig config_;
    std::atomic<bool> stop_event_{false};
    std::unique_ptr<ScreenOverlay> overlay_;
    std::vector<std::string> last_debuffs_;

    std::set<std::string> strategies_;
    std::set<std::string> debuffs_;

    // 超时常量
    static constexpr double TIMEOUT_LONG  = 90.0;
    static constexpr double TIMEOUT_SHORT = 8.0;
    static constexpr double POLL          = 0.4;
    static constexpr double STEP_DELAY    = 0.6;
    static constexpr double CLICK_DELAY   = 0.5;
    static constexpr int    MAX_RETRIES   = 3;
    static constexpr double SCENE_LOAD_DELAY = 4.0;
    static constexpr int    DEBUFF_MIN    = 1;
    static constexpr int    DEBUFF_MAX    = 4;
};

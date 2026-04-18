#pragma once
// overlay.h — 透明覆盖层模块（Win32 + GDI+）

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <Windows.h>

// 标记颜色 (ARGB)
constexpr COLORREF COLOR_OCR    = RGB(0, 255, 0);    // 绿色：OCR 识别结果
constexpr COLORREF COLOR_MATCH  = RGB(255, 102, 0);  // 橙色：模板匹配结果
constexpr COLORREF COLOR_TARGET = RGB(255, 0, 0);    // 红色：命中目标

struct Mark {
    int x1, y1, x2, y2;
    COLORREF color = COLOR_OCR;
    std::string label;

    Mark() = default;
    Mark(int x1_, int y1_, int x2_, int y2_,
         COLORREF c = COLOR_OCR, const std::string& lbl = "")
        : x1(x1_), y1(y1_), x2(x2_), y2(y2_), color(c), label(lbl) {}
};

class ScreenOverlay {
public:
    ScreenOverlay();
    ~ScreenOverlay();

    // 生命周期
    void start(int x, int y, int w, int h);
    void stop();

    // 标记操作
    void update_marks(const std::vector<Mark>& marks);
    void clear();

    // 日志与状态
    void log(const std::string& message);
    void set_step(const std::string& step);
    void clear_log();

    // 重新定位
    void reposition(int x, int y, int w, int h);

private:
    void thread_proc(int x, int y, int w, int h);
    void redraw();
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    std::thread thread_;
    std::atomic<bool> alive_{false};
    std::atomic<bool> ready_{false};

    std::mutex lock_;
    std::vector<Mark> marks_;
    std::deque<std::string> log_lines_;
    std::string current_step_;
    int win_w_ = 0, win_h_ = 0;

    static constexpr int LOG_MAX_LINES  = 20;
    // 基准值(1080p), 实际绘制时按分辨率缩放
    static constexpr int BASE_LOG_WIDTH  = 480;
    static constexpr int BASE_LOG_HEIGHT = 200;
    static constexpr UINT WM_OVERLAY_REDRAW = WM_USER + 100;
    static constexpr COLORREF TRANSPARENT_COLOR = RGB(1, 1, 1);
};

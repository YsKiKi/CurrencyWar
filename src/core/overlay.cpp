#include "core/overlay.h"
#include <iostream>
#include <chrono>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// ── 宽字符转换辅助 ──
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}

// ── ScreenOverlay ──

ScreenOverlay::ScreenOverlay() = default;

ScreenOverlay::~ScreenOverlay() {
    stop();
}

void ScreenOverlay::start(int x, int y, int w, int h) {
    alive_ = true;
    ready_ = false;
    thread_ = std::thread(&ScreenOverlay::thread_proc, this, x, y, w, h);

    // 等待窗口就绪
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready_ && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "[overlay] 覆盖层已启动: " << w << "x" << h << "+" << x << "+" << y << std::endl;
}

void ScreenOverlay::stop() {
    alive_ = false;
    if (hwnd_) {
        PostMessage(hwnd_, WM_CLOSE, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    hwnd_ = nullptr;
}

void ScreenOverlay::update_marks(const std::vector<Mark>& marks) {
    std::lock_guard<std::mutex> lk(lock_);
    marks_ = marks;
}

void ScreenOverlay::clear() {
    std::lock_guard<std::mutex> lk(lock_);
    marks_.clear();
}

void ScreenOverlay::log(const std::string& message) {
    std::lock_guard<std::mutex> lk(lock_);
    log_lines_.push_back(message);
    while ((int)log_lines_.size() > LOG_MAX_LINES) {
        log_lines_.pop_front();
    }
}

void ScreenOverlay::set_step(const std::string& step) {
    std::lock_guard<std::mutex> lk(lock_);
    current_step_ = step;
}

void ScreenOverlay::clear_log() {
    std::lock_guard<std::mutex> lk(lock_);
    log_lines_.clear();
}

void ScreenOverlay::reposition(int x, int y, int w, int h) {
    {
        std::lock_guard<std::mutex> lk(lock_);
        win_w_ = w;
        win_h_ = h;
    }
    if (hwnd_ && alive_) {
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

LRESULT CALLBACK ScreenOverlay::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* self = reinterpret_cast<ScreenOverlay*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (self) self->redraw();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // 阻止背景擦除，防止闪烁
    case WM_TIMER:
        if (self) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void ScreenOverlay::thread_proc(int x, int y, int w, int h) {
    // GDI+ 初始化
    Gdiplus::GdiplusStartupInput gdipsi;
    ULONG_PTR gdipToken;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipsi, nullptr);

    win_w_ = w;
    win_h_ = h;

    const wchar_t* cls_name = L"CurrencyWarOverlay";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = cls_name;
    wc.hbrBackground = CreateSolidBrush(TRANSPARENT_COLOR);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        cls_name, L"CurrencyWar Overlay",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );

    SetLayeredWindowAttributes(hwnd_, TRANSPARENT_COLOR, 0, LWA_COLORKEY);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);

    // 100ms 刷新定时器
    SetTimer(hwnd_, 1, 100, nullptr);

    ready_ = true;

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (!alive_) break;
    }

    KillTimer(hwnd_, 1);
    DestroyWindow(hwnd_);
    UnregisterClassW(cls_name, GetModuleHandle(nullptr));
    Gdiplus::GdiplusShutdown(gdipToken);
    alive_ = false;
}

void ScreenOverlay::redraw() {
    if (!hwnd_) return;

    HDC hdc = GetDC(hwnd_);
    RECT cr;
    GetClientRect(hwnd_, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;

    // 双缓冲
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, ch);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    // 填充透明色背景
    HBRUSH bgBrush = CreateSolidBrush(TRANSPARENT_COLOR);
    FillRect(memDC, &cr, bgBrush);
    DeleteObject(bgBrush);

    {
        std::lock_guard<std::mutex> lk(lock_);

        // 根据窗口分辨率缩放（基准: 1080p）
        float scale = (std::max)(1.0f, win_h_ / 1080.0f);
        int mark_font_sz  = static_cast<int>(16 * scale);
        int step_font_sz  = static_cast<int>(20 * scale);
        int log_font_sz   = static_cast<int>(15 * scale);
        int line_height   = static_cast<int>(20 * scale);
        int pen_width     = (std::max)(2, static_cast<int>(3 * scale));
        int log_area_w    = static_cast<int>(BASE_LOG_WIDTH * scale);
        int log_area_h    = static_cast<int>(BASE_LOG_HEIGHT * scale);

        // ── 绘制识别标记 ──
        for (auto& m : marks_) {
            HPEN pen = CreatePen(PS_SOLID, pen_width, m.color);
            HGDIOBJ oldPen = SelectObject(memDC, pen);
            HGDIOBJ oldBr = SelectObject(memDC, GetStockObject(NULL_BRUSH));

            Rectangle(memDC, m.x1, m.y1, m.x2, m.y2);

            SelectObject(memDC, oldBr);
            SelectObject(memDC, oldPen);
            DeleteObject(pen);

            if (!m.label.empty()) {
                auto wlabel = utf8_to_wide(m.label);
                SetTextColor(memDC, m.color);
                SetBkMode(memDC, TRANSPARENT);

                HFONT font = CreateFontW(mark_font_sz, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH, L"Microsoft YaHei");
                HGDIOBJ oldFont = SelectObject(memDC, font);
                TextOutW(memDC, m.x1 + 4, m.y1 - mark_font_sz - 2, wlabel.c_str(), (int)wlabel.size());
                SelectObject(memDC, oldFont);
                DeleteObject(font);
            }
        }

        // ── 左下角日志区域 ──
        int log_x1 = static_cast<int>(4 * scale);
        int log_y2 = win_h_ - static_cast<int>(4 * scale);
        int log_x2 = log_x1 + log_area_w;
        int log_y1 = log_y2 - log_area_h;

        // 半透明黑色背景（用 hatch brush 模拟）
        HBRUSH logBg = CreateSolidBrush(RGB(0, 0, 0));
        RECT logRect{log_x1, log_y1, log_x2, log_y2};
        FillRect(memDC, &logRect, logBg);
        DeleteObject(logBg);

        // 边框
        HPEN logPen = CreatePen(PS_SOLID, 1, RGB(51, 51, 51));
        HGDIOBJ oldPen = SelectObject(memDC, logPen);
        HGDIOBJ oldBr = SelectObject(memDC, GetStockObject(NULL_BRUSH));
        Rectangle(memDC, log_x1, log_y1, log_x2, log_y2);
        SelectObject(memDC, oldBr);
        SelectObject(memDC, oldPen);
        DeleteObject(logPen);

        SetBkMode(memDC, TRANSPARENT);

        int pad = static_cast<int>(8 * scale);
        int text_y_start = log_y1 + pad;

        // 步骤状态
        if (!current_step_.empty()) {
            std::string step_text = "\xe2\x96\xb6 " + current_step_;
            auto wstep = utf8_to_wide(step_text);
            SetTextColor(memDC, RGB(0, 204, 255));
            HFONT stepFont = CreateFontW(step_font_sz, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH, L"Microsoft YaHei");
            HGDIOBJ oldF = SelectObject(memDC, stepFont);
            TextOutW(memDC, log_x1 + pad, log_y1 + pad / 2, wstep.c_str(), (int)wstep.size());
            SelectObject(memDC, oldF);
            DeleteObject(stepFont);
            text_y_start = log_y1 + step_font_sz + pad;
        }

        // 日志行
        SetTextColor(memDC, RGB(255, 255, 255));
        HFONT logFont = CreateFontW(log_font_sz, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH, L"Microsoft YaHei");
        HGDIOBJ oldF = SelectObject(memDC, logFont);

        // 计算可显示行数，从最新日志开始显示（自动滚动）
        int max_visible = (log_y2 - 4 - text_y_start) / line_height;
        int total = (int)log_lines_.size();
        int start_idx = (std::max)(0, total - max_visible);
        for (int i = start_idx; i < total; ++i) {
            int y_pos = text_y_start + (i - start_idx) * line_height;
            auto wline = utf8_to_wide(log_lines_[i]);
            TextOutW(memDC, log_x1 + pad, y_pos, wline.c_str(), (int)wline.size());
        }

        SelectObject(memDC, oldF);
        DeleteObject(logFont);
    }

    BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    ReleaseDC(hwnd_, hdc);
}

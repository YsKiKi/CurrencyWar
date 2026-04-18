#include "core/window_controller.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <TlHelp32.h>
#include <Psapi.h>

// ── 辅助：通过进程名获取 PID 列表 ──
static std::vector<DWORD> find_pids_by_name(const std::string& name) {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            // 宽字符进程名转窄字符比较
            char narrow[MAX_PATH]{};
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, narrow, MAX_PATH, nullptr, nullptr);
            std::string proc(narrow);
            // 大小写不敏感比较
            std::transform(proc.begin(), proc.end(), proc.begin(), ::tolower);
            std::string target = name;
            std::transform(target.begin(), target.end(), target.begin(), ::tolower);
            if (proc == target) {
                pids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

// ── 枚举回调数据 ──
struct EnumData {
    std::vector<DWORD> target_pids;
    HWND result = nullptr;
};

static BOOL CALLBACK enum_wnd_proc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumData*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    for (DWORD target_pid : data->target_pids) {
        if (pid == target_pid) {
            data->result = hwnd;
            return FALSE; // 找到，停止枚举
        }
    }
    return TRUE;
}

// ── WindowController ──

WindowController::WindowController(const std::string& process_name)
    : process_name_(process_name) {}

bool WindowController::find_window() {
    auto pids = find_pids_by_name(process_name_);
    if (pids.empty()) return false;

    EnumData data;
    data.target_pids = pids;
    EnumWindows(enum_wnd_proc, reinterpret_cast<LPARAM>(&data));

    if (data.result) {
        hwnd_ = data.result;
        return true;
    }
    return false;
}

void WindowController::require_hwnd() const {
    if (!hwnd_) {
        throw std::runtime_error("未找到 " + process_name_ + " 窗口，请先调用 find_window()。");
    }
}

void WindowController::focus_window() {
    require_hwnd();
    if (IsIconic(hwnd_)) {
        ShowWindow(hwnd_, SW_RESTORE);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    SetForegroundWindow(hwnd_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

std::tuple<int,int,int,int> WindowController::get_rect() const {
    require_hwnd();
    RECT r;
    GetWindowRect(hwnd_, &r);
    return {r.left, r.top, r.right, r.bottom};
}

std::tuple<int,int,int,int> WindowController::get_client_rect() const {
    require_hwnd();
    POINT pt{0, 0};
    ClientToScreen(hwnd_, &pt);
    RECT cr;
    GetClientRect(hwnd_, &cr);
    return {pt.x + cr.left, pt.y + cr.top, pt.x + cr.right, pt.y + cr.bottom};
}

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 2
#endif

cv::Mat WindowController::screenshot(bool client_only) const {
    require_hwnd();
    RECT cr;
    GetClientRect(hwnd_, &cr);
    int w = cr.right - cr.left;
    int h = cr.bottom - cr.top;
    if (w <= 0 || h <= 0) return {};

    HDC hWndDC = GetDC(hwnd_);
    HDC hDC = CreateCompatibleDC(hWndDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hWndDC, w, h);
    HGDIOBJ old = SelectObject(hDC, hBitmap);

    // PrintWindow 直接从窗口获取内容，不受覆盖层影响
    UINT flags = PW_RENDERFULLCONTENT;
    if (client_only) flags |= PW_CLIENTONLY;
    BOOL ok = PrintWindow(hwnd_, hDC, flags);
    if (!ok) {
        // 回退: 从屏幕 DC 捕获
        auto [left, top, right, bottom] = client_only ? get_client_rect() : get_rect();
        HDC hScreen = GetDC(nullptr);
        BitBlt(hDC, 0, 0, w, h, hScreen, left, top, SRCCOPY);
        ReleaseDC(nullptr, hScreen);
    }

    // BITMAP → cv::Mat
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    cv::Mat img(h, w, CV_8UC4);
    int ret = GetDIBits(hDC, hBitmap, 0, h, img.data, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(hDC, old);
    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(hwnd_, hWndDC);

    if (ret == 0) {
        std::cerr << "[window] GetDIBits 失败" << std::endl;
        return {};
    }

    cv::Mat bgr;
    cv::cvtColor(img, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

std::pair<int,int> WindowController::abs_pos(int x, int y, bool relative) const {
    if (relative) {
        auto [cl, ct, cr, cb] = get_client_rect();
        return {cl + x, ct + y};
    }
    return {x, y};
}

void WindowController::send_mouse_input(DWORD flags) {
    INPUT inp{};
    inp.type = INPUT_MOUSE;
    inp.mi.dwFlags = flags;
    SendInput(1, &inp, sizeof(INPUT));
}

void WindowController::click(int x, int y, bool relative,
                              const std::string& button, double delay) {
    require_hwnd();
    auto [ax, ay] = abs_pos(x, y, relative);
    focus_window();

    SetCursorPos(ax, ay);
    auto delay_ms = std::chrono::milliseconds(static_cast<int>(delay * 1000));
    std::this_thread::sleep_for(delay_ms);

    DWORD down_flag, up_flag;
    if (button == "right") {
        down_flag = MOUSEEVENTF_RIGHTDOWN;
        up_flag = MOUSEEVENTF_RIGHTUP;
    } else if (button == "middle") {
        down_flag = MOUSEEVENTF_MIDDLEDOWN;
        up_flag = MOUSEEVENTF_MIDDLEUP;
    } else {
        down_flag = MOUSEEVENTF_LEFTDOWN;
        up_flag = MOUSEEVENTF_LEFTUP;
    }

    send_mouse_input(down_flag);
    std::this_thread::sleep_for(delay_ms);
    send_mouse_input(up_flag);
}

void WindowController::move_mouse(int x, int y, bool relative) {
    auto [ax, ay] = abs_pos(x, y, relative);
    SetCursorPos(ax, ay);
}

void WindowController::double_click(int x, int y, bool relative) {
    click(x, y, relative);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    click(x, y, relative);
}

std::string WindowController::title() const {
    if (!hwnd_) return "";
    wchar_t buf[512]{};
    GetWindowTextW(hwnd_, buf, 512);
    char narrow[1024]{};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, narrow, 1024, nullptr, nullptr);
    return narrow;
}

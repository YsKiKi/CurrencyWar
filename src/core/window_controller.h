#pragma once
// window_controller.h — 窗口查找、截图、鼠标点击控制（Windows）

#include <string>
#include <tuple>
#include <optional>
#include <Windows.h>
#include <opencv2/core.hpp>

class WindowController {
public:
    explicit WindowController(const std::string& process_name = "StarRail.exe");

    // 查找窗口
    bool find_window();

    // 窗口操作
    void focus_window();
    std::tuple<int,int,int,int> get_rect() const;
    std::tuple<int,int,int,int> get_client_rect() const;

    // 截图（返回 BGR cv::Mat）
    cv::Mat screenshot(bool client_only = true) const;

    // 鼠标操作
    void click(int x, int y, bool relative = true,
               const std::string& button = "left", double delay = 0.05);
    void move_mouse(int x, int y, bool relative = true);
    void double_click(int x, int y, bool relative = true);

    // 属性
    std::string title() const;
    HWND hwnd() const { return hwnd_; }
    bool valid() const { return hwnd_ != nullptr; }

private:
    void require_hwnd() const;
    std::pair<int,int> abs_pos(int x, int y, bool relative) const;
    static void send_mouse_input(DWORD flags);

    std::string process_name_;
    HWND hwnd_ = nullptr;
};

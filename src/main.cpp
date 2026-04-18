/**
 * main.cpp
 * CurrencyWar C++ 入口。
 *
 * 用法：
 *   CurrencyWar.exe             → 启动 GUI
 *   CurrencyWar.exe --console   → 启动 GUI 并显示控制台
 *   CurrencyWar.exe --nogui     → 无 GUI，直接运行
 */

#include "core/config.h"
#include "core/window_controller.h"
#include "core/ocr_engine.h"
#include "core/image_matcher.h"
#include "core/bot.h"
#include "gui/main_window.h"

#include <QApplication>
#include <QIcon>
#include <Windows.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <functional>

// ── 日志流重定向到 GUI ──
class GuiLogBuf : public std::streambuf {
public:
    using LogFn = std::function<void(const std::string&)>;
    GuiLogBuf(std::streambuf* original, LogFn fn)
        : original_(original), log_fn_(std::move(fn)) {}

protected:
    int overflow(int c) override {
        if (c == EOF) return c;
        if (original_) original_->sputc(static_cast<char>(c));
        std::lock_guard<std::mutex> lk(mtx_);
        if (c == '\n') {
            if (log_fn_ && !line_.empty()) log_fn_(line_);
            line_.clear();
        } else {
            line_ += static_cast<char>(c);
        }
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (original_) original_->sputn(s, n);
        std::lock_guard<std::mutex> lk(mtx_);
        for (std::streamsize i = 0; i < n; ++i) {
            if (s[i] == '\n') {
                if (log_fn_ && !line_.empty()) log_fn_(line_);
                line_.clear();
            } else {
                line_ += s[i];
            }
        }
        return n;
    }
private:
    std::streambuf* original_;
    LogFn log_fn_;
    std::string line_;
    std::mutex mtx_;
};

// ── DPI 感知 ──
static void set_dpi_aware() {
    using SetDpiFunc = HRESULT(WINAPI*)(int);
    HMODULE shcore = LoadLibraryA("Shcore.dll");
    if (shcore) {
        auto fn = reinterpret_cast<SetDpiFunc>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (fn) {
            fn(2); // PROCESS_PER_MONITOR_DPI_AWARE
            return;
        }
    }
    SetProcessDPIAware();
}

// ── 管理员权限检查 ──
static bool is_admin() {
    BOOL admin = FALSE;
    PSID group = nullptr;
    SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
        CheckTokenMembership(nullptr, group, &admin);
        FreeSid(group);
    }
    return admin != FALSE;
}

static void request_admin() {
    if (is_admin()) return;

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
    ExitProcess(0);
}

// ── 初始化组件 ──
struct Components {
    std::unique_ptr<WindowController> window;
    std::unique_ptr<OCREngine> ocr;
    std::unique_ptr<ImageMatcher> matcher;
};

static std::unique_ptr<Components> init_components() {
    auto comp = std::make_unique<Components>();
    comp->window = std::make_unique<WindowController>("StarRail.exe");

    std::cout << "[main] 正在查找 StarRail.exe 窗口…" << std::endl;
    if (!comp->window->find_window()) {
        std::cerr << "[main] 未找到 StarRail.exe，请先启动游戏后再运行本程序。" << std::endl;
        return nullptr;
    }

    std::cout << "[main] 找到窗口: " << comp->window->title() << std::endl;
    comp->window->focus_window();

    std::cout << "[main] 初始化 OCR 引擎 ……" << std::endl;
    try {
        comp->ocr = std::make_unique<OCREngine>("res/ocr");
    } catch (const std::exception& e) {
        std::cerr << "[main] OCR 初始化失败: " << e.what() << std::endl;
        std::cerr << "[main] 请确认 res/ocr/ 目录下已放置 PaddleOCR 模型文件。" << std::endl;
        return nullptr;
    }

    comp->matcher = std::make_unique<ImageMatcher>(0.85f);
    return comp;
}

// ── 无 GUI 模式 ──
static void main_nogui() {
    set_dpi_aware();
    auto comp = init_components();
    if (!comp) {
        ExitProcess(1);
    }

    auto config = AppConfig::load();
    CurrencyWarBot bot(*comp->window, *comp->ocr, *comp->matcher, config);
    bot.run();
}

// ── GUI 模式 ──
static void main_gui(int argc, char* argv[], bool show_console) {
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon("res/icon.ico"));

    std::unique_ptr<Components> comp;
    std::unique_ptr<CurrencyWarBot> bot;
    std::thread bot_thread;

    auto screenshot_fn = [&]() -> cv::Mat {
        if (!comp || !comp->window) {
            auto wc = std::make_unique<WindowController>("StarRail.exe");
            if (!wc->find_window()) {
                throw std::runtime_error("未找到 StarRail.exe 窗口");
            }
            wc->focus_window();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            return wc->screenshot(true);
        }
        comp->window->focus_window();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return comp->window->screenshot(true);
    };

    CurrencyWarGUI* gui = nullptr;

    auto on_start = [&](const AppConfig& config) {
        // 如果之前的线程还在运行，先等待它结束
        if (bot_thread.joinable()) {
            if (bot) bot->stop();
            bot_thread.join();
        }

        if (!comp) {
            try {
                comp = init_components();
            } catch (const std::exception& e) {
                std::cerr << "[main] 初始化失败: " << e.what() << std::endl;
                if (gui) gui->show_error(std::string("初始化失败: ") + e.what());
                return;
            }
            if (!comp) {
                if (gui) gui->show_error("初始化失败: 未找到 StarRail.exe，请先启动游戏。");
                return;
            }
        }

        bot = std::make_unique<CurrencyWarBot>(
            *comp->window, *comp->ocr, *comp->matcher, config);
        bot->on_stopped = [gui]() {
            if (gui) gui->set_stopped();
        };

        bot_thread = std::thread([&bot, gui]() {
            try {
                bot->run();
            } catch (const std::exception& e) {
                std::cerr << "[main] Bot 异常: " << e.what() << std::endl;
                if (gui) gui->show_error(std::string("Bot 异常: ") + e.what());
                return;
            }
            if (gui) gui->set_stopped();
        });
    };

    auto on_stop = [&]() {
        if (bot) bot->stop();
    };

    CurrencyWarGUI gui_obj(on_start, on_stop, screenshot_fn);
    gui = &gui_obj;

    // 重新识别窗口回调
    gui_obj.set_redetect_callback([&]() -> bool {
        if (!comp) {
            comp = std::make_unique<Components>();
            comp->window = std::make_unique<WindowController>("StarRail.exe");
        }
        bool ok = comp->window->find_window();
        if (ok) {
            std::cout << "[main] 重新识别到窗口: " << comp->window->title() << std::endl;
        } else {
            std::cerr << "[main] 未找到 StarRail.exe 窗口。" << std::endl;
        }
        return ok;
    });

    // 重新加载列表回调
    gui_obj.set_reload_callback([&]() {
        // 清除现有 bot
        std::cout << "[main] 策略和Debuff列表已重新加载。" << std::endl;
    });

    // 安装日志重定向到 GUI
    std::unique_ptr<GuiLogBuf> cout_buf, cerr_buf;
    std::streambuf* orig_cout = std::cout.rdbuf();
    std::streambuf* orig_cerr = std::cerr.rdbuf();

    auto log_fn = [gui](const std::string& msg) {
        if (gui) gui->append_log(msg);
    };
    cout_buf = std::make_unique<GuiLogBuf>(show_console ? orig_cout : nullptr, log_fn);
    cerr_buf = std::make_unique<GuiLogBuf>(show_console ? orig_cerr : nullptr, log_fn);
    std::cout.rdbuf(cout_buf.get());
    std::cerr.rdbuf(cerr_buf.get());

    gui_obj.run();

    // 恢复原始 streambuf
    std::cout.rdbuf(orig_cout);
    std::cerr.rdbuf(orig_cerr);

    // GUI 退出后，确保 bot 线程安全退出
    if (bot) bot->stop();
    if (bot_thread.joinable()) bot_thread.join();
}

// ── WinMain 入口 ──
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int) {
    // Qt6 已自动设置 DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    // set_dpi_aware() 会与 Qt 冲突，仅在 nogui 模式使用
    request_admin();

    // 设置工作目录
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring dir(exe_path);
    auto pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        dir = dir.substr(0, pos);
        SetCurrentDirectoryW(dir.c_str());

    }

    // 解析命令行
    std::string cmd(lpCmdLine);
    bool nogui = (cmd.find("--nogui") != std::string::npos);
    bool show_console = (cmd.find("--console") != std::string::npos) || nogui;

    // 控制台：nogui 或 --console 时分配并显示，否则分配但隐藏
    AllocConsole();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    if (!show_console) {
        ShowWindow(GetConsoleWindow(), SW_HIDE);
    }

    if (nogui) {
        main_nogui();
    } else {
        int argc = 1;
        char* argv[] = { const_cast<char*>("CurrencyWar"), nullptr };
        main_gui(argc, argv, show_console);
    }

    return 0;
}

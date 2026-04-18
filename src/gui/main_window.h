#pragma once
// main_window.h — Qt6 GUI 主窗口

#include "core/config.h"
#include <QMainWindow>
#include <QTabWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QStatusBar>
#include <QGroupBox>
#include <QCheckBox>
#include <QCompleter>
#include <QLabel>
#include <QDialog>
#include <QPixmap>
#include <QMessageBox>
#include <QTextEdit>
#include <QIcon>

#include <functional>
#include <vector>
#include <string>
#include <opencv2/core.hpp>

// ── 快捷键编辑框 ──
class HotkeyEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit HotkeyEdit(const QString& text = "delete", QWidget* parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
private:
    bool listening_ = false;
};

// ── 带下拉框的列表编辑器 ──
class SearchableListEditor : public QGroupBox {
    Q_OBJECT
public:
    SearchableListEditor(const QString& title,
                         const std::vector<std::string>& candidates,
                         QWidget* parent = nullptr);

    std::vector<std::string> get_items() const;
    void set_items(const std::vector<std::string>& items);
    void set_candidates(const std::vector<std::string>& candidates);

private slots:
    void add_item();
    void remove_selected();

private:
    QComboBox* combo_;
    QListWidget* list_;
};

// ── 区域框选对话框 ──
class RegionSelectDialog : public QDialog {
    Q_OBJECT
public:
    RegionSelectDialog(const cv::Mat& screenshot, QWidget* parent = nullptr);

signals:
    void region_selected(int x, int y, int w, int h);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void repaint_rect();
    QPixmap pixmap_;
    QLabel* label_;
    double scale_ = 1.0;
    QPoint start_;
    QPoint current_;
    bool dragging_ = false;
};

// ── 区域选择器 ──
class RegionSelector : public QGroupBox {
    Q_OBJECT
public:
    using ScreenshotFn = std::function<cv::Mat()>;

    RegionSelector(const QString& title,
                   ScreenshotFn screenshot_fn = nullptr,
                   QWidget* parent = nullptr);

    RegionConfig get_region() const;
    void set_region(const RegionConfig& r);

private slots:
    void do_select();
    void on_region_selected(int x, int y, int w, int h);

private:
    QSpinBox* make_spin();
    QSpinBox* x_spin_;
    QSpinBox* y_spin_;
    QSpinBox* w_spin_;
    QSpinBox* h_spin_;
    ScreenshotFn screenshot_fn_;
};

// ── 主窗口 ──
class CurrencyWarGUI : public QObject {
    Q_OBJECT
public:
    using StartCallback = std::function<void(const AppConfig&)>;
    using StopCallback  = std::function<void()>;
    using ScreenshotFn  = std::function<cv::Mat()>;
    using RedetectCallback = std::function<bool()>;
    using ReloadCallback   = std::function<void()>;

    CurrencyWarGUI(StartCallback on_start = nullptr,
                   StopCallback on_stop = nullptr,
                   ScreenshotFn screenshot_fn = nullptr);

    void run();

    // 线程安全：通知 GUI 作业已结束
    void set_stopped();
    // 线程安全：显示错误信息并停止
    void show_error(const std::string& msg);
    // 线程安全：追加日志到运行日志标签
    void append_log(const std::string& msg);

    // 设置额外回调
    void set_redetect_callback(RedetectCallback cb) { on_redetect_ = std::move(cb); }
    void set_reload_callback(ReloadCallback cb) { on_reload_ = std::move(cb); }

signals:
    void stopped_signal();
    void error_signal(QString msg);
    void log_signal(QString msg);

private slots:
    void on_external_stop();
    void on_error_show(QString msg);
    void on_log_append(QString msg);
    void toggle_run();
    void save_config();
    void save_config_as();
    void load_config();
    void redetect_window();
    void reload_lists();

private:
    void build_ui();
    AppConfig collect_config() const;
    void apply_config(const AppConfig& cfg);

    StartCallback on_start_;
    StopCallback on_stop_;
    ScreenshotFn screenshot_fn_;
    RedetectCallback on_redetect_;
    ReloadCallback on_reload_;
    bool running_ = false;

    QMainWindow* win_ = nullptr;
    QTabWidget* tabs_ = nullptr;

    SearchableListEditor* target_envs_editor_ = nullptr;
    SearchableListEditor* unwanted_debuffs_editor_ = nullptr;
    SearchableListEditor* wanted_buffs_editor_ = nullptr;

    QSpinBox* min_rounds_spin_ = nullptr;
    QSpinBox* max_attempts_spin_ = nullptr;

    RegionSelector* env_region_sel_ = nullptr;
    RegionSelector* debuff_region_sel_ = nullptr;

    HotkeyEdit* stop_key_edit_ = nullptr;
    QCheckBox* debug_overlay_check_ = nullptr;
    QPushButton* start_btn_ = nullptr;
    QStatusBar* statusbar_ = nullptr;
    QTextEdit* log_text_ = nullptr;

    std::vector<std::string> all_strategies_;
    std::vector<std::string> all_debuffs_;
};

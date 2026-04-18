#include "gui/main_window.h"
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QKeySequence>
#include <QStringListModel>
#include <QScrollBar>
#include <QStyleFactory>
#include <QDesktopServices>
#include <QUrl>
#include <algorithm>

// ── Qt Key → keyboard 键名映射 ──
static QString qt_key_to_name(int key) {
    static const QMap<int, QString> map = {
        {Qt::Key_F1, "f1"}, {Qt::Key_F2, "f2"}, {Qt::Key_F3, "f3"},
        {Qt::Key_F4, "f4"}, {Qt::Key_F5, "f5"}, {Qt::Key_F6, "f6"},
        {Qt::Key_F7, "f7"}, {Qt::Key_F8, "f8"}, {Qt::Key_F9, "f9"},
        {Qt::Key_F10, "f10"}, {Qt::Key_F11, "f11"}, {Qt::Key_F12, "f12"},
        {Qt::Key_Escape, "esc"}, {Qt::Key_Tab, "tab"},
        {Qt::Key_Backspace, "backspace"}, {Qt::Key_Return, "enter"},
        {Qt::Key_Enter, "enter"}, {Qt::Key_Insert, "insert"},
        {Qt::Key_Delete, "delete"}, {Qt::Key_Home, "home"},
        {Qt::Key_End, "end"}, {Qt::Key_PageUp, "page up"},
        {Qt::Key_PageDown, "page down"},
        {Qt::Key_Up, "up"}, {Qt::Key_Down, "down"},
        {Qt::Key_Left, "left"}, {Qt::Key_Right, "right"},
        {Qt::Key_Space, "space"}, {Qt::Key_Pause, "pause"},
    };
    auto it = map.find(key);
    if (it != map.end()) return it.value();
    return {};
}

// ── HotkeyEdit ──

HotkeyEdit::HotkeyEdit(const QString& text, QWidget* parent)
    : QLineEdit(text, parent)
{
    setReadOnly(true);
    setPlaceholderText(QString::fromUtf8("点击此处，然后按下快捷键…"));
}

void HotkeyEdit::mousePressEvent(QMouseEvent* event) {
    listening_ = true;
    setText(QString::fromUtf8("请按下快捷键…"));
    setFocus();
    QLineEdit::mousePressEvent(event);
}

void HotkeyEdit::keyPressEvent(QKeyEvent* event) {
    if (!listening_) return;
    int key = event->key();
    if (key == Qt::Key_Control || key == Qt::Key_Shift ||
        key == Qt::Key_Alt || key == Qt::Key_Meta) return;

    QStringList parts;
    auto mods = event->modifiers();
    if (mods & Qt::ControlModifier) parts << "ctrl";
    if (mods & Qt::AltModifier) parts << "alt";
    if (mods & Qt::ShiftModifier) parts << "shift";

    QString key_name = qt_key_to_name(key);
    if (key_name.isEmpty()) {
        QString text = event->text().trimmed().toLower();
        if (!text.isEmpty() && text[0].isPrint())
            key_name = text;
        else
            key_name = QKeySequence(key).toString().toLower();
    }
    parts << key_name;

    setText(parts.join("+"));
    listening_ = false;
}

void HotkeyEdit::focusOutEvent(QFocusEvent* event) {
    if (listening_) {
        listening_ = false;
        if (text() == QString::fromUtf8("请按下快捷键…"))
            setText("delete");
    }
    QLineEdit::focusOutEvent(event);
}

// ── SearchableListEditor ──

SearchableListEditor::SearchableListEditor(const QString& title,
                                           const std::vector<std::string>& candidates,
                                           QWidget* parent)
    : QGroupBox(title, parent)
{
    auto* layout = new QVBoxLayout(this);

    // 下拉框行
    auto* search_row = new QHBoxLayout();
    combo_ = new QComboBox();
    combo_->setEditable(true);
    combo_->setInsertPolicy(QComboBox::NoInsert);
    combo_->lineEdit()->setPlaceholderText(QString::fromUtf8("搜索并选择…"));
    combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // 填充候选项
    QStringList cand_list;
    for (auto& c : candidates) cand_list << QString::fromUtf8(c.c_str());
    std::sort(cand_list.begin(), cand_list.end());
    combo_->addItems(cand_list);
    combo_->setCurrentIndex(-1);

    // 设置补全器用于搜索过滤
    auto* completer = new QCompleter(cand_list, this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    combo_->setCompleter(completer);

    auto* add_btn = new QPushButton(QString::fromUtf8("添加"));
    add_btn->setFixedWidth(60);
    connect(add_btn, &QPushButton::clicked, this, &SearchableListEditor::add_item);

    search_row->addWidget(combo_);
    search_row->addWidget(add_btn);
    layout->addLayout(search_row);

    // 列表
    list_ = new QListWidget();
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(list_);

    // 删除按钮
    auto* del_btn = new QPushButton(QString::fromUtf8("删除选中"));
    connect(del_btn, &QPushButton::clicked, this, &SearchableListEditor::remove_selected);
    layout->addWidget(del_btn, 0, Qt::AlignRight);
}

void SearchableListEditor::add_item() {
    QString text = combo_->currentText().trimmed();
    if (text.isEmpty()) return;
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->text() == text) {
            combo_->setCurrentIndex(-1);
            combo_->clearEditText();
            return;
        }
    }
    list_->addItem(text);
    combo_->setCurrentIndex(-1);
    combo_->clearEditText();
}

void SearchableListEditor::remove_selected() {
    auto items = list_->selectedItems();
    for (int i = items.size() - 1; i >= 0; --i) {
        delete list_->takeItem(list_->row(items[i]));
    }
}

std::vector<std::string> SearchableListEditor::get_items() const {
    std::vector<std::string> result;
    for (int i = 0; i < list_->count(); ++i) {
        result.push_back(list_->item(i)->text().toUtf8().constData());
    }
    return result;
}

void SearchableListEditor::set_items(const std::vector<std::string>& items) {
    list_->clear();
    for (auto& item : items) {
        list_->addItem(QString::fromUtf8(item.c_str()));
    }
}

void SearchableListEditor::set_candidates(const std::vector<std::string>& candidates) {
    combo_->clear();
    QStringList cand_list;
    for (auto& c : candidates) cand_list << QString::fromUtf8(c.c_str());
    std::sort(cand_list.begin(), cand_list.end());
    combo_->addItems(cand_list);
    combo_->setCurrentIndex(-1);
    combo_->clearEditText();

    auto* completer = new QCompleter(cand_list, this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    combo_->setCompleter(completer);
}

// ── RegionSelectDialog ──

RegionSelectDialog::RegionSelectDialog(const cv::Mat& screenshot, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("拖拽框选区域 — 释放鼠标确认"));
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    setCursor(Qt::CrossCursor);

    int iw = screenshot.cols, ih = screenshot.rows;
    auto* screen = QApplication::primaryScreen();
    int sw = 1920, sh = 1080;
    if (screen) {
        auto sg = screen->availableGeometry();
        sw = sg.width(); sh = sg.height();
    }
    scale_ = std::min({sw * 0.9 / iw, sh * 0.85 / ih, 1.0});
    int disp_w = static_cast<int>(iw * scale_);
    int disp_h = static_cast<int>(ih * scale_);

    // cv::Mat BGR → QPixmap
    cv::Mat rgb;
    cv::cvtColor(screenshot, rgb, cv::COLOR_BGR2RGB);
    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(disp_w, disp_h));

    QImage qimg(resized.data, disp_w, disp_h,
                static_cast<int>(resized.step), QImage::Format_RGB888);
    pixmap_ = QPixmap::fromImage(qimg.copy());

    label_ = new QLabel();
    label_->setPixmap(pixmap_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(label_);
    setFixedSize(disp_w, disp_h);
}

void RegionSelectDialog::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        start_ = event->pos();
        current_ = event->pos();
        dragging_ = true;
    }
}

void RegionSelectDialog::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        current_ = event->pos();
        repaint_rect();
    }
}

void RegionSelectDialog::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        QPoint end = event->pos();
        int x1 = static_cast<int>(std::min(start_.x(), end.x()) / scale_);
        int y1 = static_cast<int>(std::min(start_.y(), end.y()) / scale_);
        int x2 = static_cast<int>(std::max(start_.x(), end.x()) / scale_);
        int y2 = static_cast<int>(std::max(start_.y(), end.y()) / scale_);
        int w = x2 - x1, h = y2 - y1;
        if (w > 5 && h > 5) {
            emit region_selected(x1, y1, w, h);
            accept();
        }
    }
}

void RegionSelectDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) reject();
}

void RegionSelectDialog::repaint_rect() {
    QPixmap pm = pixmap_.copy();
    QPainter painter(&pm);
    QPen pen(QColor(255, 0, 0), 2);
    painter.setPen(pen);
    if (dragging_) {
        painter.drawRect(QRect(start_, current_).normalized());
    }
    painter.end();
    label_->setPixmap(pm);
}

// ── RegionSelector ──

RegionSelector::RegionSelector(const QString& title, ScreenshotFn screenshot_fn,
                               QWidget* parent)
    : QGroupBox(title, parent), screenshot_fn_(std::move(screenshot_fn))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);

    x_spin_ = make_spin();
    y_spin_ = make_spin();
    w_spin_ = make_spin();
    h_spin_ = make_spin();

    struct LabelSpin { QString label; QSpinBox* spin; };
    for (auto& [lbl, spin] : std::vector<LabelSpin>{
        {"X:", x_spin_}, {"Y:", y_spin_}, {"W:", w_spin_}, {"H:", h_spin_}}) {
        auto* l = new QLabel(lbl);
        l->setFixedWidth(18);
        layout->addWidget(l);
        layout->addWidget(spin);
        layout->addSpacing(6);
    }

    auto* select_btn = new QPushButton(QString::fromUtf8("框选"));
    connect(select_btn, &QPushButton::clicked, this, &RegionSelector::do_select);
    layout->addWidget(select_btn);
    layout->addStretch();
}

QSpinBox* RegionSelector::make_spin() {
    auto* spin = new QSpinBox();
    spin->setRange(0, 9999);
    spin->setFixedWidth(70);
    return spin;
}

RegionConfig RegionSelector::get_region() const {
    return {x_spin_->value(), y_spin_->value(),
            w_spin_->value(), h_spin_->value()};
}

void RegionSelector::set_region(const RegionConfig& r) {
    x_spin_->setValue(r.x);
    y_spin_->setValue(r.y);
    w_spin_->setValue(r.w);
    h_spin_->setValue(r.h);
}

void RegionSelector::do_select() {
    if (!screenshot_fn_) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("截图功能不可用"));
        return;
    }
    cv::Mat img;
    try {
        img = screenshot_fn_();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, QString::fromUtf8("截屏失败"),
                              QString::fromUtf8(e.what()));
        return;
    }
    if (img.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("截图为空"));
        return;
    }
    auto* dlg = new RegionSelectDialog(img, window());
    connect(dlg, &RegionSelectDialog::region_selected,
            this, &RegionSelector::on_region_selected);
    dlg->exec();
    dlg->deleteLater();
}

void RegionSelector::on_region_selected(int x, int y, int w, int h) {
    set_region({x, y, w, h});
}

// ── CurrencyWarGUI ──

CurrencyWarGUI::CurrencyWarGUI(StartCallback on_start, StopCallback on_stop,
                                 ScreenshotFn screenshot_fn)
    : on_start_(std::move(on_start)), on_stop_(std::move(on_stop)),
      screenshot_fn_(std::move(screenshot_fn))
{
    all_strategies_ = load_name_list("res/strategy.txt");
    all_debuffs_ = load_name_list("res/debuff.txt");

    connect(this, &CurrencyWarGUI::stopped_signal,
            this, &CurrencyWarGUI::on_external_stop, Qt::QueuedConnection);
    connect(this, &CurrencyWarGUI::error_signal,
            this, &CurrencyWarGUI::on_error_show, Qt::QueuedConnection);
    connect(this, &CurrencyWarGUI::log_signal,
            this, &CurrencyWarGUI::on_log_append, Qt::QueuedConnection);

    build_ui();
}

void CurrencyWarGUI::build_ui() {
    // 使用 Qt 6.8+ 现代 Windows 风格
    for (auto name : {"windows11", "modernwindows"}) {
        if (auto* s = QStyleFactory::create(name)) {
            QApplication::setStyle(s);
            break;
        }
    }

    win_ = new QMainWindow();
    win_->setWindowTitle(QString::fromUtf8("货币战争自动化配置"));
    win_->setWindowIcon(QIcon("res/icon.ico"));
    win_->setMinimumSize(800, 560);

    auto* central = new QWidget();
    win_->setCentralWidget(central);
    auto* main_layout = new QVBoxLayout(central);

    tabs_ = new QTabWidget();
    main_layout->addWidget(tabs_);

    // ── Tab 1: 策略与Debuff ──
    auto* tab_lists = new QWidget();
    auto* tab_lists_layout = new QHBoxLayout(tab_lists);

    target_envs_editor_ = new SearchableListEditor(
        QString::fromUtf8("需要的投资策略"), all_strategies_);
    unwanted_debuffs_editor_ = new SearchableListEditor(
        QString::fromUtf8("不需要的Debuff"), all_debuffs_);
    wanted_buffs_editor_ = new SearchableListEditor(
        QString::fromUtf8("需要的Debuff"), all_debuffs_);

    tab_lists_layout->addWidget(target_envs_editor_);
    tab_lists_layout->addWidget(unwanted_debuffs_editor_);
    tab_lists_layout->addWidget(wanted_buffs_editor_);
    tabs_->addTab(tab_lists, QString::fromUtf8("策略/Debuff"));

    // ── Tab 2: OCR / 区域 ──
    auto* tab_ocr = new QWidget();
    auto* tab_ocr_layout = new QVBoxLayout(tab_ocr);

    auto* ocr_group = new QGroupBox(QString::fromUtf8("OCR 稳定扫描参数"));
    auto* ocr_form = new QFormLayout(ocr_group);

    min_rounds_spin_ = new QSpinBox();
    min_rounds_spin_->setRange(1, 100);
    min_rounds_spin_->setValue(5);
    ocr_form->addRow(QString::fromUtf8("最少连续比对次数:"), min_rounds_spin_);

    max_attempts_spin_ = new QSpinBox();
    max_attempts_spin_->setRange(1, 500);
    max_attempts_spin_->setValue(25);
    ocr_form->addRow(QString::fromUtf8("最大尝试次数:"), max_attempts_spin_);

    debug_overlay_check_ = new QCheckBox(QString::fromUtf8("显示debug框体"));
    debug_overlay_check_->setChecked(true);
    debug_overlay_check_->setToolTip(QString::fromUtf8("开启后，覆盖层会显示OCR识别区域的边框和模板匹配框"));
    ocr_form->addRow(debug_overlay_check_);

    tab_ocr_layout->addWidget(ocr_group);

    env_region_sel_ = new RegionSelector(
        QString::fromUtf8("投资策略识别区域"), screenshot_fn_);
    tab_ocr_layout->addWidget(env_region_sel_);

    debuff_region_sel_ = new RegionSelector(
        QString::fromUtf8("Debuff识别区域"), screenshot_fn_);
    tab_ocr_layout->addWidget(debuff_region_sel_);

    tab_ocr_layout->addStretch();
    tabs_->addTab(tab_ocr, QString::fromUtf8("OCR/识别区域"));

    // ── Tab 3: 快捷键 ──
    auto* tab_hotkey = new QWidget();
    auto* tab_hk_layout = new QVBoxLayout(tab_hotkey);

    auto* hk_group = new QGroupBox(QString::fromUtf8("快捷键配置"));
    auto* hk_form = new QFormLayout(hk_group);

    stop_key_edit_ = new HotkeyEdit("delete");
    hk_form->addRow(QString::fromUtf8("停止作业快捷键:"), stop_key_edit_);

    tab_hk_layout->addWidget(hk_group);

    // 按钮图片替换提示
    auto* btn_img_group = new QGroupBox(QString::fromUtf8("按钮图片"));
    auto* btn_img_layout = new QVBoxLayout(btn_img_group);

    auto* hint_label = new QLabel(QString::fromUtf8(
        "如果按钮识别不准，请截屏替换 res/buttons 文件夹下的按钮图片。\n"
        "注意：截图必须使用原有文件名（如 确认.png、下一步_开局.png 等），\n"
        "不要修改文件名，否则程序无法识别。"));
    hint_label->setWordWrap(true);
    btn_img_layout->addWidget(hint_label);

    auto* open_folder_btn = new QPushButton(QString::fromUtf8("打开按钮图片文件夹"));
    connect(open_folder_btn, &QPushButton::clicked, this, [this]() {
        QString folder = QApplication::applicationDirPath() + "/res/buttons";
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    btn_img_layout->addWidget(open_folder_btn, 0, Qt::AlignLeft);

    tab_hk_layout->addWidget(btn_img_group);
    tab_hk_layout->addStretch();
    tabs_->addTab(tab_hotkey, QString::fromUtf8("快捷键"));

    // ── Tab 4: 运行日志 ──
    auto* tab_log = new QWidget();
    auto* tab_log_layout = new QVBoxLayout(tab_log);

    log_text_ = new QTextEdit();
    log_text_->setReadOnly(true);
    tab_log_layout->addWidget(log_text_);

    auto* log_btn_layout = new QHBoxLayout();
    auto* clear_log_btn = new QPushButton(QString::fromUtf8("清空日志"));
    connect(clear_log_btn, &QPushButton::clicked, this, [this]() {
        log_text_->clear();
    });
    log_btn_layout->addStretch();
    log_btn_layout->addWidget(clear_log_btn);
    tab_log_layout->addLayout(log_btn_layout);

    tabs_->addTab(tab_log, QString::fromUtf8("运行日志"));

    // ── 底部按钮栏 ──
    auto* btn_layout = new QHBoxLayout();

    auto* save_btn = new QPushButton(QString::fromUtf8("保存配置"));
    connect(save_btn, &QPushButton::clicked, this, &CurrencyWarGUI::save_config);
    btn_layout->addWidget(save_btn);

    auto* save_as_btn = new QPushButton(QString::fromUtf8("配置另存为"));
    connect(save_as_btn, &QPushButton::clicked, this, &CurrencyWarGUI::save_config_as);
    btn_layout->addWidget(save_as_btn);

    auto* load_btn = new QPushButton(QString::fromUtf8("加载配置"));
    connect(load_btn, &QPushButton::clicked, this, &CurrencyWarGUI::load_config);
    btn_layout->addWidget(load_btn);

    auto* redetect_btn = new QPushButton(QString::fromUtf8("重新识别窗口"));
    connect(redetect_btn, &QPushButton::clicked, this, &CurrencyWarGUI::redetect_window);
    btn_layout->addWidget(redetect_btn);

    auto* reload_btn = new QPushButton(QString::fromUtf8("重新加载列表"));
    connect(reload_btn, &QPushButton::clicked, this, &CurrencyWarGUI::reload_lists);
    btn_layout->addWidget(reload_btn);

    btn_layout->addStretch();

    start_btn_ = new QPushButton(QString::fromUtf8("开始作业"));
    start_btn_->setObjectName("startBtn");
    start_btn_->setMinimumWidth(120);
    connect(start_btn_, &QPushButton::clicked, this, &CurrencyWarGUI::toggle_run);
    btn_layout->addWidget(start_btn_);

    main_layout->addLayout(btn_layout);

    statusbar_ = new QStatusBar();
    win_->setStatusBar(statusbar_);
    statusbar_->showMessage(QString::fromUtf8("就绪"));

    // 加载默认配置
    apply_config(AppConfig::load());
}

AppConfig CurrencyWarGUI::collect_config() const {
    AppConfig cfg;
    cfg.target_envs = target_envs_editor_->get_items();
    cfg.unwanted_debuffs = unwanted_debuffs_editor_->get_items();
    cfg.wanted_buffs = wanted_buffs_editor_->get_items();
    cfg.min_confirm_rounds = min_rounds_spin_->value();
    cfg.max_confirm_attempts = max_attempts_spin_->value();
    cfg.env_region = env_region_sel_->get_region();
    cfg.debuff_region = debuff_region_sel_->get_region();
    cfg.show_debug_overlay = debug_overlay_check_->isChecked();
    QString hk = stop_key_edit_->text().trimmed();
    cfg.stop_hotkey = hk.isEmpty() ? "delete" : hk.toUtf8().constData();
    return cfg;
}

void CurrencyWarGUI::apply_config(const AppConfig& cfg) {
    target_envs_editor_->set_items(cfg.target_envs);
    unwanted_debuffs_editor_->set_items(cfg.unwanted_debuffs);
    wanted_buffs_editor_->set_items(cfg.wanted_buffs);
    min_rounds_spin_->setValue(cfg.min_confirm_rounds);
    max_attempts_spin_->setValue(cfg.max_confirm_attempts);
    env_region_sel_->set_region(cfg.env_region);
    debuff_region_sel_->set_region(cfg.debuff_region);
    debug_overlay_check_->setChecked(cfg.show_debug_overlay);
    stop_key_edit_->setText(QString::fromUtf8(cfg.stop_hotkey.c_str()));
}

void CurrencyWarGUI::save_config() {
    auto cfg = collect_config();
    cfg.save();  // 保存到默认路径 config.json
    statusbar_->showMessage(QString::fromUtf8("配置已保存到 config.json"));
}

void CurrencyWarGUI::save_config_as() {
    QString path = QFileDialog::getSaveFileName(
        win_, QString::fromUtf8("配置另存为"), "config.json",
        QString::fromUtf8("JSON 配置文件 (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;
    auto cfg = collect_config();
    cfg.save(path.toUtf8().constData());
    statusbar_->showMessage(QString::fromUtf8("配置已保存到 ") + path);
}

void CurrencyWarGUI::load_config() {
    QString path = QFileDialog::getOpenFileName(
        win_, QString::fromUtf8("加载配置"), "",
        QString::fromUtf8("JSON 配置文件 (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;
    try {
        auto cfg = AppConfig::load(path.toUtf8().constData());
        apply_config(cfg);
        statusbar_->showMessage(QString::fromUtf8("已加载配置 ") + path);
    } catch (const std::exception& e) {
        QMessageBox::critical(win_, QString::fromUtf8("加载失败"),
                              QString::fromUtf8(e.what()));
    }
}

void CurrencyWarGUI::toggle_run() {
    if (!running_) {
        auto cfg = collect_config();
        cfg.save(); // 自动保存到默认位置
        running_ = true;
        start_btn_->setText(QString::fromUtf8("停止作业"));
        statusbar_->showMessage(QString::fromUtf8("作业运行中…"));
        if (on_start_) on_start_(cfg);
    } else {
        running_ = false;
        start_btn_->setText(QString::fromUtf8("开始作业"));
        statusbar_->showMessage(QString::fromUtf8("已停止"));
        if (on_stop_) on_stop_();
    }
}

void CurrencyWarGUI::set_stopped() {
    running_ = false;
    emit stopped_signal();
}

void CurrencyWarGUI::show_error(const std::string& msg) {
    running_ = false;
    emit error_signal(QString::fromUtf8(msg.c_str()));
}

void CurrencyWarGUI::append_log(const std::string& msg) {
    emit log_signal(QString::fromUtf8(msg.c_str()));
}

void CurrencyWarGUI::on_external_stop() {
    start_btn_->setText(QString::fromUtf8("开始作业"));
    statusbar_->showMessage(QString::fromUtf8("作业已完成"));
}

void CurrencyWarGUI::on_error_show(QString msg) {
    start_btn_->setText(QString::fromUtf8("开始作业"));
    statusbar_->showMessage(msg);
    QMessageBox::warning(win_, QString::fromUtf8("作业失败"), msg);
}

void CurrencyWarGUI::on_log_append(QString msg) {
    if (log_text_) {
        log_text_->append(msg);
        // 自动滚动到底部
        auto* sb = log_text_->verticalScrollBar();
        if (sb) sb->setValue(sb->maximum());
    }
}

void CurrencyWarGUI::redetect_window() {
    if (on_redetect_) {
        bool ok = on_redetect_();
        if (ok) {
            statusbar_->showMessage(QString::fromUtf8("已重新识别到游戏窗口"));
        } else {
            QMessageBox::warning(win_, QString::fromUtf8("提示"),
                                 QString::fromUtf8("未找到 StarRail.exe 窗口，请确认游戏已启动。"));
        }
    } else {
        statusbar_->showMessage(QString::fromUtf8("重新识别功能未就绪"));
    }
}

void CurrencyWarGUI::reload_lists() {
    all_strategies_ = load_name_list("res/strategy.txt");
    all_debuffs_ = load_name_list("res/debuff.txt");

    target_envs_editor_->set_candidates(all_strategies_);
    unwanted_debuffs_editor_->set_candidates(all_debuffs_);
    wanted_buffs_editor_->set_candidates(all_debuffs_);

    if (on_reload_) on_reload_();

    statusbar_->showMessage(QString::fromUtf8("已重新加载策略(") +
        QString::number(all_strategies_.size()) + QString::fromUtf8("个)和Debuff(") +
        QString::number(all_debuffs_.size()) + QString::fromUtf8("个)"));
}

void CurrencyWarGUI::run() {
    win_->show();
    QApplication::exec();
}

# CurrencyWar C++

崩坏：星穹铁道「货币战争」自动化工具 — C++ 重写版。

## 功能

- **自动化流程**：自动进入货币战争 → 识别投资环境 → 目标匹配 → 退出重置循环
- **OCR 文字识别**：基于 PaddleOCR PP-OCRv4 (Paddle Inference C++) 的中文 OCR，用于识别投资策略和 Debuff
- **模板匹配**：基于 OpenCV 的按钮图像识别
- **透明覆盖层**：实时显示识别标记、日志和当前步骤
- **GUI 配置界面**：Qt6 图形界面，支持策略/Debuff 搜索联想、区域框选、快捷键配置
- **热键停止**：可自定义停止快捷键（默认 Delete）

## 依赖

| 库 | 用途 |
|---|---|
| OpenCV 4.x | 图像处理、模板匹配 |
| Paddle Inference C++ (CPU) | PaddleOCR PP-OCRv4 推理 |
| nlohmann/json | JSON 配置文件解析 |
| Qt6 Widgets | GUI 界面 |

## 构建

### 使用 vcpkg + CMake

```powershell
# 1. 安装 vcpkg（如果尚未安装）
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat

# 2. 安装 vcpkg 依赖
.\vcpkg\vcpkg install opencv4 nlohmann-json qt6 --triplet x64-windows

# 3. 下载 Paddle Inference C++ 库
# 从 https://www.paddlepaddle.org.cn/inference/master/guides/install/download_lib.html
# 选择 Windows / CPU / C++ 版本下载并解压

# 4. 构建项目
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DPADDLE_INFERENCE_DIR=<paddle_inference解压路径>
cmake --build build --config Release
```

### PaddleOCR 模型文件

需要在 `res/ocr/` 目录下放置以下文件：

- `ch_PP-OCRv4_det_infer/` — 文本检测模型
- `ch_PP-OCRv4_rec_infer/` — 文本识别模型
- `ppocr_keys_v1.txt` — 字符字典

详见 `res/ocr/README.txt` 中的下载地址。

## 目录结构

```
CMakeLists.txt          # CMake 构建配置
vcpkg.json              # vcpkg 依赖清单
src/
  main.cpp              # 入口（GUI/无GUI模式）
  core/
    config.h/cpp        # JSON 配置管理
    window_controller.h/cpp  # 窗口查找、截图、鼠标控制
    ocr_engine.h/cpp    # PaddleOCR (Paddle Inference) OCR 封装
    image_matcher.h/cpp # OpenCV 模板匹配
    overlay.h/cpp       # Win32 透明覆盖层
    bot.h/cpp           # 自动化主控逻辑
  gui/
    main_window.h/cpp   # Qt6 GUI 界面
res/
  buttons/              # 按钮模板图像
  strategy.txt          # 合法投资策略名称列表
  debuff.txt            # 已知 Debuff 名称列表
  ocr/                  # PaddleOCR 模型文件 (det/rec/dict)
```

## 用法

```
CurrencyWar.exe          # 启动 GUI 配置界面
CurrencyWar.exe --nogui  # 无 GUI，使用 config.json 直接运行
```

## 从 Python 版本迁移

原 Python 版本位于 `old_python/` 目录。C++ 版本完整重写了所有功能模块：

| Python 模块 | C++ 对应 | 说明 |
|---|---|---|
| `core/window.py` | `core/window_controller.h/cpp` | Win32 API 直接调用（无 pywin32 依赖） |
| `core/ocr.py` | `core/ocr_engine.h/cpp` | PaddleOCR (Python) → Paddle Inference C++ |
| `core/vision.py` | `core/image_matcher.h/cpp` | OpenCV C++ API |
| `core/overlay.py` | `core/overlay.h/cpp` | tkinter → Win32 GDI+ 覆盖层 |
| `core/config.py` | `core/config.h/cpp` | dataclass → struct + nlohmann/json |
| `core/bot.py` | `core/bot.h/cpp` | 完整流程逻辑重写 |
| `gui/app.py` | `gui/main_window.h/cpp` | PyQt6 → Qt6 C++ |

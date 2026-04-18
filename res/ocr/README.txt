==================================================================
PaddleOCR PP-OCRv4 模型文件说明
==================================================================

本目录需要放置以下模型文件和字典，程序才能正常运行 OCR 功能。

── 目录结构 ──

res/ocr/
├── ch_PP-OCRv4_det_infer/
│   ├── inference.pdmodel
│   └── inference.pdiparams
├── ch_PP-OCRv4_rec_infer/
│   ├── inference.pdmodel
│   └── inference.pdiparams
├── ppocr_keys_v1.txt
└── README.txt (本文件)

── 下载地址 ──

1. 检测模型 (ch_PP-OCRv4_det_infer):
   https://paddleocr.bj.bcebos.com/PP-OCRv4/chinese/ch_PP-OCRv4_det_infer.tar

2. 识别模型 (ch_PP-OCRv4_rec_infer):
   https://paddleocr.bj.bcebos.com/PP-OCRv4/chinese/ch_PP-OCRv4_rec_infer.tar

3. 字典文件 (ppocr_keys_v1.txt):
   https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/ppocr_keys_v1.txt

── 安装步骤 ──

1. 下载上述三个文件
2. 将 .tar 文件解压到本目录
3. 将 ppocr_keys_v1.txt 放到本目录
4. 确保目录结构如上所示

── Paddle Inference C++ 库 ──

构建本项目还需要 Paddle Inference C++ 预编译库:
https://www.paddlepaddle.org.cn/inference/master/guides/install/download_lib.html

选择 Windows / CPU / C++ 版本下载解压，
然后在 CMake 配置时指定:
  cmake -DPADDLE_INFERENCE_DIR=<解压路径> ..

==================================================================

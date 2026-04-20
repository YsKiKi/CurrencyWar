# ============================================================================
# CurrencyWar - CMake 构建配置
# ============================================================================
#
# 依赖：OpenCV 4.x, Qt 6.x, nlohmann-json, Paddle Inference
#
# ── 构建方式 ──────────────────────────────────────────────────────────────
#
#  1) 默认 (vcpkg)  — 无需任何额外配置，适用于 CI / 本地依赖未配置的用户：
#       cmake --preset default
#       cmake --build --preset default
#
#  2) 本地环境      — 若系统已安装 OpenCV / Qt，可跳过 vcpkg 编译，使用本地环境：
#
#       在项目根目录创建 CMakeUserPresets.json：
#
# ── CMakeUserPresets.json 示例 ──────────────────────────────────────────────
#       {
#         "version": 6,
#         "configurePresets": [{
#           "name": "local",
#           "inherits": "default",
#           "cacheVariables": {
#             "LOCAL_OPENCV_DIR": "C:/your/opencv/build",
#             "LOCAL_QT_DIR":    "D:/your/Qt/6.x.x/msvc2022_64"
#           }
#         }],
#         "buildPresets": [{
#           "name": "local",
#           "configurePreset": "local",
#           "configuration": "Release"
#         }]
#       }
# ── FileEnd ──────────────────────────────────────────────────────────────
#
#     然后：
#       cmake --preset local
#       cmake --build --preset local
# =========================================================================

cmake_minimum_required(VERSION 3.20)
project(CurrencyWar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── MSVC UTF-8 ──
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# ── 本地安装路径（通过 CMakeUserPresets.json 或 cmake -D 设置） ──
# 留空表示使用 vcpkg 提供的依赖（云端默认行为）
set(LOCAL_OPENCV_DIR "" CACHE PATH "本地 OpenCV 路径")
set(LOCAL_QT_DIR "" CACHE PATH "本地 Qt 路径")

if(LOCAL_OPENCV_DIR AND EXISTS "${LOCAL_OPENCV_DIR}/OpenCVConfig.cmake")
    set(OpenCV_DIR "${LOCAL_OPENCV_DIR}" CACHE PATH "" FORCE)
    message(STATUS "Using local OpenCV: ${LOCAL_OPENCV_DIR}")
endif()

if(LOCAL_QT_DIR AND EXISTS "${LOCAL_QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
    list(APPEND CMAKE_PREFIX_PATH "${LOCAL_QT_DIR}")
    message(STATUS "Using local Qt: ${LOCAL_QT_DIR}")
endif()

# ── 依赖 ──
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(nlohmann_json REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)

# ── Paddle Inference ──
# 设置 PADDLE_INFERENCE_DIR 指向解压后的 paddle_inference 目录
# 示例: cmake -DPADDLE_INFERENCE_DIR=C:/paddle_inference ..
# 若未设置，将使用 third_party/paddle_inference 下的存根
set(PADDLE_INFERENCE_DIR "" CACHE PATH "Path to Paddle Inference C++ library")
if(NOT PADDLE_INFERENCE_DIR)
    if(DEFINED ENV{PADDLE_INFERENCE_DIR})
        set(PADDLE_INFERENCE_DIR $ENV{PADDLE_INFERENCE_DIR})
    endif()
endif()

set(USE_PADDLE_STUB OFF)
if(NOT PADDLE_INFERENCE_DIR)
    # Check for third_party stub
    set(PADDLE_STUB_DIR "${CMAKE_SOURCE_DIR}/third_party/paddle_inference")
    if(EXISTS "${PADDLE_STUB_DIR}/paddle/include/paddle_inference_api.h")
        message(WARNING
            "PADDLE_INFERENCE_DIR not set, using third_party/paddle_inference stub for compile check.\n"
            "For production, download the full Paddle Inference library from:\n"
            "https://www.paddlepaddle.org.cn/inference/master/guides/install/download_lib.html")
        set(PADDLE_INFERENCE_DIR ${PADDLE_STUB_DIR})
        set(USE_PADDLE_STUB ON)
    else()
        message(FATAL_ERROR
            "Please set PADDLE_INFERENCE_DIR to the Paddle Inference C++ library root.\n"
            "Download: https://www.paddlepaddle.org.cn/inference/master/guides/install/download_lib.html\n"
            "Usage: cmake -DPADDLE_INFERENCE_DIR=<path> ..")
    endif()
endif()

set(PADDLE_INC_DIR  "${PADDLE_INFERENCE_DIR}/paddle/include")
set(PADDLE_LIB_DIR  "${PADDLE_INFERENCE_DIR}/paddle/lib")
set(PADDLE_THIRD_LIB_DIR "${PADDLE_INFERENCE_DIR}/third_party/install")

# Paddle Inference 头文件
include_directories(${PADDLE_INC_DIR})

if(NOT USE_PADDLE_STUB)
    # 真实 Paddle Inference 库目录
    link_directories(${PADDLE_LIB_DIR})

    # OneDNN (MKLDNN)
    if(EXISTS "${PADDLE_THIRD_LIB_DIR}/onednn/lib")
        include_directories("${PADDLE_THIRD_LIB_DIR}/onednn/include")
        link_directories("${PADDLE_THIRD_LIB_DIR}/onednn/lib")
    elseif(EXISTS "${PADDLE_THIRD_LIB_DIR}/mkldnn/lib")
        include_directories("${PADDLE_THIRD_LIB_DIR}/mkldnn/include")
        link_directories("${PADDLE_THIRD_LIB_DIR}/mkldnn/lib")
    endif()

    # MKLML (Intel Math Kernel Library)
    if(EXISTS "${PADDLE_THIRD_LIB_DIR}/mklml/lib")
        include_directories("${PADDLE_THIRD_LIB_DIR}/mklml/include")
        link_directories("${PADDLE_THIRD_LIB_DIR}/mklml/lib")
    endif()
endif()

# ── GDI+ / Win32 ──
set(WIN32_LIBS
    user32 gdi32 gdiplus shell32 psapi dwmapi ole32
)

# ── 源文件 ──
set(CORE_SOURCES
    src/core/config.cpp
    src/core/window_controller.cpp
    src/core/ocr_engine.cpp
    src/core/image_matcher.cpp
    src/core/overlay.cpp
    src/core/bot.cpp
)

set(CORE_HEADERS
    src/core/config.h
    src/core/window_controller.h
    src/core/ocr_engine.h
    src/core/image_matcher.h
    src/core/overlay.h
    src/core/bot.h
)

set(GUI_SOURCES
    src/gui/main_window.cpp
)

set(GUI_HEADERS
    src/gui/main_window.h
)

add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    src/app.rc
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    ${GUI_SOURCES}
    ${GUI_HEADERS}
)

# 存根模式: 将存根实现编译进目标
if(USE_PADDLE_STUB)
    target_sources(${PROJECT_NAME} PRIVATE
        "${PADDLE_INFERENCE_DIR}/paddle/lib/paddle_inference_stub.cpp"
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(USE_PADDLE_STUB)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${OpenCV_LIBS}
        nlohmann_json::nlohmann_json
        Qt6::Widgets Qt6::Core Qt6::Gui
        ${WIN32_LIBS}
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${OpenCV_LIBS}
        paddle_inference common
        nlohmann_json::nlohmann_json
        Qt6::Widgets Qt6::Core Qt6::Gui
        ${WIN32_LIBS}
    )

    # 复制 Paddle Inference DLL 到输出目录
    file(GLOB PADDLE_DLLS "${PADDLE_LIB_DIR}/*.dll")
    if(EXISTS "${PADDLE_THIRD_LIB_DIR}/onednn/lib")
        file(GLOB ONEDNN_DLLS "${PADDLE_THIRD_LIB_DIR}/onednn/lib/*.dll")
        list(APPEND PADDLE_DLLS ${ONEDNN_DLLS})
    endif()
    if(EXISTS "${PADDLE_THIRD_LIB_DIR}/mkldnn/lib")
        file(GLOB MKLDNN_DLLS "${PADDLE_THIRD_LIB_DIR}/mkldnn/lib/*.dll")
        list(APPEND PADDLE_DLLS ${MKLDNN_DLLS})
    endif()
    if(EXISTS "${PADDLE_THIRD_LIB_DIR}/mklml/lib")
        file(GLOB MKLML_DLLS "${PADDLE_THIRD_LIB_DIR}/mklml/lib/*.dll")
        list(APPEND PADDLE_DLLS ${MKLML_DLLS})
    endif()
    foreach(DLL ${PADDLE_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endforeach()
endif()

# UAC 管理员权限
set_target_properties(${PROJECT_NAME} PROPERTIES
    LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
)

# ── 复制 OpenCV DLL 到输出目录 ──
if(LOCAL_OPENCV_DIR)
    # 本地 OpenCV: 从 bin 目录复制 DLL
    get_filename_component(_OCV_ROOT "${OpenCV_DIR}" DIRECTORY)
    set(_OCV_DLL_CANDIDATES
        "${OpenCV_DIR}/x64/vc16/bin"
        "${OpenCV_DIR}/x64/vc17/bin"
        "${OpenCV_DIR}/bin"
        "${OpenCV_DIR}/bin/Release"
    )
    foreach(_OCV_BIN ${_OCV_DLL_CANDIDATES})
        file(GLOB _OCV_DLLS "${_OCV_BIN}/opencv_*.dll")
        if(_OCV_DLLS)
            foreach(DLL ${_OCV_DLLS})
                # 跳过 debug 版本的 DLL
                string(REGEX MATCH "d\\.dll$" _IS_DEBUG "${DLL}")
                if(NOT _IS_DEBUG)
                    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                    )
                endif()
            endforeach()
            break()
        endif()
    endforeach()
endif()

# ── 复制 Qt 核心 DLL 到输出目录 ──
if(LOCAL_QT_DIR)
    set(_QT_DLLS_NEEDED Qt6Core Qt6Gui Qt6Widgets)
    foreach(_QT_MOD ${_QT_DLLS_NEEDED})
        set(_QT_DLL "${LOCAL_QT_DIR}/bin/${_QT_MOD}.dll")
        if(EXISTS "${_QT_DLL}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_QT_DLL}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            )
        endif()
    endforeach()
endif()

# 复制 res/ 到输出目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/res"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/res"
    COMMAND ${CMAKE_COMMAND} -E remove -f
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/res/icon.ico"
    COMMENT "Copying res/ to output directory"
)

# 复制 Qt 平台插件到输出目录
get_target_property(QT6_CORE_LOCATION Qt6::Core LOCATION)
get_filename_component(QT6_BIN_DIR "${QT6_CORE_LOCATION}" DIRECTORY)
# 搜索 Qt 插件目录（兼容本地安装和 vcpkg）
set(QT6_PLUGINS_CANDIDATES
    "${QT6_BIN_DIR}/../plugins/platforms"
    "${QT6_BIN_DIR}/../Qt6/plugins/platforms"
    "${QT6_BIN_DIR}/../../Qt6/plugins/platforms"
    "${QT6_BIN_DIR}/../../plugins/platforms"
)
foreach(CANDIDATE ${QT6_PLUGINS_CANDIDATES})
    if(EXISTS "${CANDIDATE}/qwindows.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CANDIDATE}/qwindows.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms/qwindows.dll"
            COMMENT "Copying Qt platform plugin qwindows.dll"
        )
        break()
    endif()
endforeach()

# 复制 Qt 风格插件 (modernwindows)
set(QT6_STYLES_CANDIDATES
    "${QT6_BIN_DIR}/../Qt6/plugins/styles"
    "${QT6_BIN_DIR}/../plugins/styles"
    "${QT6_BIN_DIR}/../../Qt6/plugins/styles"
)
foreach(CANDIDATE ${QT6_STYLES_CANDIDATES})
    if(EXISTS "${CANDIDATE}/qmodernwindowsstyle.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/styles"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CANDIDATE}/qmodernwindowsstyle.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/styles/qmodernwindowsstyle.dll"
            COMMENT "Copying Qt style plugin qmodernwindowsstyle.dll"
        )
        break()
    endif()
endforeach()



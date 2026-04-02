cmake_minimum_required(VERSION 3.20)
project(TracerTracker VERSION 1.0.0 LANGUAGES CXX)

# 使用 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 自动工具（MOC、UIC、RCC）
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 查找 Qt6 必需组件
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    OpenGL
    OpenGLWidgets
    Network
)

# SerialPort 为可选组件（若未安装则禁用串口功能）
find_package(Qt6 OPTIONAL_COMPONENTS SerialPort)
if(Qt6SerialPort_FOUND)
    message(STATUS "Qt6SerialPort found, serial port support enabled")
    set(HAVE_QT_SERIAL TRUE)
else()
    message(STATUS "Qt6SerialPort NOT found, serial port support disabled")
    set(HAVE_QT_SERIAL FALSE)
endif()

# 查找 Eigen3（可选，若未找到则回退到内置简单矩阵实现）
find_package(Eigen3 QUIET)
if(Eigen3_FOUND)
    message(STATUS "Eigen3 found: ${EIGEN3_INCLUDE_DIR}")
    set(HAVE_EIGEN3 TRUE)
else()
    message(STATUS "Eigen3 not found, using built-in math utilities")
    set(HAVE_EIGEN3 FALSE)
endif()

# 收集所有源文件
set(SOURCES
    src/main.cpp
    src/config/ConfigLoader.cpp
    src/ins/MathUtils.cpp
    src/ins/Ahrs.cpp
    src/ins/Filters.cpp
    src/ins/PoseProcessor.cpp
    src/io/Ms901mStreamParser.cpp
    src/io/DataReceiver.cpp
    src/ui/opengl/Viewer3D.cpp
    src/ui/opengl/GridRenderer.cpp
    src/ui/opengl/TrackRenderer.cpp
    src/ui/ToolBar.cpp
    src/ui/DebugConsole.cpp
    src/ui/AttitudeWidget.cpp
    src/ui/SensorChartPanel.cpp
    src/ui/SensorInfoOverlay.cpp
    src/ui/ViewOrientationGizmo.cpp
    src/ui/MainWindow.cpp
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接 Qt6 库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Network
)

# 若 SerialPort 可用则链接并启用编译宏
if(HAVE_QT_SERIAL)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::SerialPort)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_QT_SERIAL_PORT)
endif()

# 包含 Eigen3（若可用）
if(Eigen3_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_EIGEN3)
endif()

# 添加 src 目录作为包含路径（使相对 include 可用）
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Windows 特定设置
if(WIN32)
    # 确保 Windows API 可用（WM_NCHITTEST 等）
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    # Release 模式下不显示控制台窗口
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

# 输出构建信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Qt6 version: ${Qt6_VERSION}")

#include <QApplication>
#include <QSurfaceFormat>
#include <QFont>

#include "ui/MainWindow.h"

// ============================================================
// main.cpp — TracerTracker 程序入口
//
// 初始化顺序：
//   1. 配置高 DPI 缩放（Qt 6 自动 DPI）
//   2. 设置 OpenGL 3.3 Core Profile Surface Format
//   3. 设置全局字体（微软雅黑）
//   4. 创建并显示主窗口
// ============================================================

int main(int argc, char* argv[]) {
    // Qt 6 默认高 DPI，启用像素精确缩放
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

    QApplication app(argc, argv);
    app.setApplicationName("TracerTracker");
    app.setOrganizationName("TracerTracker");

    // 全局默认字体（中文界面）
    QFont defaultFont("Microsoft YaHei", 10);
    app.setFont(defaultFont);

    // 配置 OpenGL 3.3 Core Profile（QOpenGLWidget 要求）
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(4);          // 4x MSAA 抗锯齿
    QSurfaceFormat::setDefaultFormat(fmt);

    // 创建主窗口
    MainWindow window;
    window.show();

    return app.exec();
}

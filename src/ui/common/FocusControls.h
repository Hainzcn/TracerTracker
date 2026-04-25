#pragma once
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QWheelEvent>

// ============================================================
// FocusControls.h — 焦点态滚轮过滤的通用表单控件
//
// 普通 QComboBox / QDoubleSpinBox 在父控件滚动条内会"偷"走滚轮事件，
// 导致在面板里滚动时鼠标悬停经过下拉框/数值框时滚动被打断。这两个
// 子类在未获焦点时把 wheel 事件 ignore，让父级 QScrollArea 接管。
//
// 任何嵌入侧滑面板/对话框的表单都可以直接复用，保持一致的交互手感。
// ============================================================

// 注：未声明 Q_OBJECT —— 仅覆写虚函数 wheelEvent，没有新增 signal/slot/property，
// 通过基类 QObject 元对象完成生命周期管理即可。这样把类完整放在 header 里也无需
// 让 AUTOMOC 单独再生成 moc 文件，简化构建依赖。
class FocusComboBox : public QComboBox {
public:
    explicit FocusComboBox(QWidget* parent = nullptr) : QComboBox(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }
protected:
    void wheelEvent(QWheelEvent* ev) override {
        if (hasFocus()) QComboBox::wheelEvent(ev);
        else            ev->ignore();
    }
};

class FocusSpinBox : public QDoubleSpinBox {
public:
    explicit FocusSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }
protected:
    void wheelEvent(QWheelEvent* ev) override {
        if (hasFocus()) QDoubleSpinBox::wheelEvent(ev);
        else            ev->ignore();
    }
};

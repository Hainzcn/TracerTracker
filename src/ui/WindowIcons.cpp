#include "WindowIcons.h"

#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QColor>
#include <QRectF>

namespace WindowIcons {
namespace {

constexpr int kIconExtent = 10;       // 图标主体边长
constexpr qreal kIconStroke = 1;      // 描边线宽
const QColor kIconColor("#cccccc");
const QColor kIconCornerColor("#787878");

QRectF centeredStrokeRect(const QSize& size, int extent,
                          qreal offsetX = 0.0, qreal offsetY = 0.0) {
    return QRectF((size.width() - extent) * 0.5 + offsetX,
                  (size.height() - extent) * 0.5 + offsetY,
                  extent - 1.0,
                  extent - 1.0);
}

QRect centeredPixelBounds(const QSize& size, int extent) {
    return QRect((size.width() - extent) / 2,
                 (size.height() - extent) / 2,
                 extent, extent);
}

QPen makeIconPen() {
    QPen pen(kIconColor, kIconStroke, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    pen.setCosmetic(true);
    return pen;
}

void highlightRectCorners(QPainter& painter, const QRectF& rect) {
    const int left = qRound(rect.left());
    const int top = qRound(rect.top());
    const int right = qRound(rect.right());
    const int bottom = qRound(rect.bottom());

    painter.fillRect(left,  top,    1, 1, kIconCornerColor);
    painter.fillRect(right, top,    1, 1, kIconCornerColor);
    painter.fillRect(left,  bottom, 1, 1, kIconCornerColor);
    painter.fillRect(right, bottom, 1, 1, kIconCornerColor);
}

} // namespace

QIcon makeMinimizeIcon() {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(makeIconPen());

    const QRectF rect = centeredStrokeRect(pixmap.size(), kIconExtent);
    painter.drawLine(QPointF(rect.left(),  rect.center().y()),
                     QPointF(rect.right(), rect.center().y()));
    return QIcon(pixmap);
}

QIcon makeMaximizeIcon() {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(makeIconPen());
    painter.setBrush(Qt::NoBrush);

    const QRectF rect = centeredStrokeRect(pixmap.size(), kIconExtent);
    painter.drawRect(rect);
    highlightRectCorners(painter, rect);
    return QIcon(pixmap);
}

QIcon makeRestoreIcon() {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(makeIconPen());
    painter.setBrush(Qt::NoBrush);

    const QRectF backRect(5.5, 3.5, 6.0, 6.0);
    const QRectF frontRect(3.5, 5.5, 6.0, 6.0);

    painter.drawLine(QPointF(backRect.left(),       backRect.top()),
                     QPointF(backRect.right(),      backRect.top()));
    painter.drawLine(QPointF(backRect.right(),      backRect.top()),
                     QPointF(backRect.right(),      backRect.bottom()));
    painter.drawLine(QPointF(backRect.right() - 2.0, backRect.bottom()),
                     QPointF(backRect.right(),       backRect.bottom()));

    painter.drawLine(QPointF(frontRect.left(),  frontRect.top()),
                     QPointF(frontRect.right(), frontRect.top()));
    painter.drawLine(QPointF(frontRect.left(),  frontRect.top()),
                     QPointF(frontRect.left(),  frontRect.bottom()));
    painter.drawLine(QPointF(frontRect.right(), frontRect.top()),
                     QPointF(frontRect.right(), frontRect.bottom()));
    painter.drawLine(QPointF(frontRect.left(),  frontRect.bottom()),
                     QPointF(frontRect.right(), frontRect.bottom()));

    painter.fillRect(int(backRect.left()),   int(backRect.top()),    1, 1, kIconCornerColor);
    painter.fillRect(int(backRect.right()),  int(backRect.top()),    1, 1, kIconCornerColor);
    painter.fillRect(int(frontRect.left()),  int(frontRect.top()),   1, 1, kIconCornerColor);
    painter.fillRect(int(frontRect.right()), int(frontRect.top()),   1, 1, kIconCornerColor);
    painter.fillRect(int(frontRect.left()),  int(frontRect.bottom()), 1, 1, kIconCornerColor);
    painter.fillRect(int(frontRect.right()), int(frontRect.bottom()), 1, 1, kIconCornerColor);
    return QIcon(pixmap);
}

QIcon makeCloseIcon() {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kIconColor);

    // 逐像素绘制保证偶数边长时叉号中心形成 2×2 结构，并保持完整 10px 高度。
    const QRect rect = centeredPixelBounds(pixmap.size(), kIconExtent);
    for (int i = 0; i < rect.width(); ++i) {
        painter.fillRect(rect.left()  + i, rect.top() + i, 1, 1, kIconColor);
        painter.fillRect(rect.right() - i, rect.top() + i, 1, 1, kIconColor);
    }
    return QIcon(pixmap);
}

} // namespace WindowIcons

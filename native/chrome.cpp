#include "chrome.h"
#include "palette.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QWindow>
#include <algorithm>

namespace {
class WindowButton : public QPushButton {
public:
    WindowButton(const QString &action, const QString &tip, FrameWindow *owner)
        : owner_(owner), action_(action) {
        setObjectName(action); setToolTip(tip); setAccessibleName(tip);
        setFixedSize(30, 26); setCursor(Qt::PointingHandCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || hasFocus() || isDown()) {
            p.setPen(Qt::NoPen); p.setBrush(action_ == "windowClose" ? QColor("#873b4a") : QColor("#30434a"));
            p.drawRoundedRect(rect(), 6, 6);
        }
        p.setPen(QPen(QColor("#b8cccf"), 1.4, Qt::SolidLine, Qt::RoundCap));
        if (action_ == "windowClose") { p.drawLine(10, 8, 20, 18); p.drawLine(20, 8, 10, 18); }
        else if (action_ == "windowMinimize") p.drawLine(10, 15, 20, 15);
        else {
            if (owner_->isMaximized()) { p.drawLine(12, 7, 22, 7); p.drawLine(22, 7, 22, 15); }
            p.drawRect(QRectF(10, 9, 10, 9));
        }
    }
private:
    FrameWindow *owner_;
    QString action_;
};
}

FrameWindow::FrameWindow() {
    setPalette(playerPalette());
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint
                   | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
    setMouseTracking(true);
    qApp->installEventFilter(this);
}
void FrameWindow::toggleMaximized() { isMaximized() ? showNormal() : showMaximized(); }
Qt::Edges FrameWindow::resizeEdges(const QPoint &point) const {
    if (isMaximized() || isFullScreen() || !rect().contains(point)) return {};
    constexpr int grip = 6;
    Qt::Edges edges;
    if (point.x() < grip) edges |= Qt::LeftEdge;
    if (point.x() >= width() - grip) edges |= Qt::RightEdge;
    if (point.y() < grip) edges |= Qt::TopEdge;
    if (point.y() >= height() - grip) edges |= Qt::BottomEdge;
    return edges;
}
bool FrameWindow::eventFilter(QObject *object, QEvent *event) {
    const auto widget = qobject_cast<QWidget *>(object);
    if (!widget || widget->window() != this) return false;
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress) {
        const auto mouse = static_cast<QMouseEvent *>(event);
        const auto edges = resizeEdges(mapFromGlobal(mouse->globalPosition().toPoint()));
        if (event->type() == QEvent::MouseMove) {
            if (edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge)) setCursor(Qt::SizeFDiagCursor);
            else if (edges == (Qt::TopEdge | Qt::RightEdge) || edges == (Qt::BottomEdge | Qt::LeftEdge)) setCursor(Qt::SizeBDiagCursor);
            else if (edges & (Qt::LeftEdge | Qt::RightEdge)) setCursor(Qt::SizeHorCursor);
            else if (edges & (Qt::TopEdge | Qt::BottomEdge)) setCursor(Qt::SizeVerCursor);
            else unsetCursor();
        } else if (mouse->button() == Qt::LeftButton && edges && windowHandle()) {
            if (windowHandle()->startSystemResize(edges)) return true;
        }
    } else if (event->type() == QEvent::Leave && object == this) unsetCursor();
    return false;
}
void FrameWindow::paintEvent(QPaintEvent *) {
    QPainter p(this); p.fillRect(rect(), QColor("#171e24"));
    p.setPen(QColor("#364a50")); p.drawRect(rect().adjusted(0, 0, -1, -1));
}

TitleBar::TitleBar(FrameWindow *owner, bool showIcon) : QWidget(owner), owner_(owner), showIcon_(showIcon) {
    setObjectName("titleBar"); setFixedHeight(30); setMouseTracking(true);
    auto layout = new QHBoxLayout(this); layout->setContentsMargins(0, 2, 0, 2); layout->setSpacing(2);
    layout->addStretch();
    auto minimize = new WindowButton("windowMinimize", QString::fromUtf8("Minimize"), owner);
    auto maximize = new WindowButton("windowMaximize", QString::fromUtf8("Maximize / restore"), owner);
    auto close = new WindowButton("windowClose", QString::fromUtf8("Close"), owner);
    layout->addWidget(minimize); layout->addWidget(maximize); layout->addWidget(close);
    connect(minimize, &QPushButton::clicked, owner, &QWidget::showMinimized);
    connect(maximize, &QPushButton::clicked, owner, &FrameWindow::toggleMaximized);
    connect(close, &QPushButton::clicked, owner, &QWidget::close);
}
void TitleBar::setTitle(const QString &title) {
    if (title_ == title) return;
    title_ = title; setToolTip(title); update();
}
void TitleBar::addAction(QWidget *button) { static_cast<QHBoxLayout *>(layout())->insertWidget(1, button); }
void TitleBar::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    if (showIcon_) owner_->windowIcon().paint(&p, QRect(0, 5, 20, 20));
    const int left = showIcon_ ? 30 : 4;
    const int available = std::max(0, layout()->itemAt(1)->widget()->x() - left - 8);
    p.setFont(QFont("Segoe UI", 10, QFont::DemiBold)); p.setPen(QColor("#dce5e8"));
    p.drawText(QRect(left, 0, available, height()), Qt::AlignVCenter,
               p.fontMetrics().elidedText(title_, Qt::ElideRight, available));
}
void TitleBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && owner_->windowHandle() && owner_->windowHandle()->startSystemMove()) event->accept();
    else QWidget::mousePressEvent(event);
}
void TitleBar::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) { owner_->toggleMaximized(); event->accept(); }
    else QWidget::mouseDoubleClickEvent(event);
}

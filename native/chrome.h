#pragma once
#include <QWidget>

// Common frameless shell; content keeps its own layout and paint implementation.
class FrameWindow : public QWidget {
public:
    FrameWindow();
    void toggleMaximized();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
private:
    Qt::Edges resizeEdges(const QPoint &point) const;
};

class TitleBar : public QWidget {
public:
    explicit TitleBar(FrameWindow *owner, bool showIcon = true);
    void setTitle(const QString &title);
    void addAction(QWidget *button);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
private:
    FrameWindow *owner_;
    QString title_;
    bool showIcon_;
};

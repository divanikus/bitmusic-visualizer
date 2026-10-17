#pragma once
#include <QSlider>

class Timeline : public QSlider {
public:
    explicit Timeline(QWidget *parent = nullptr) : QSlider(Qt::Horizontal, parent) {}
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    int valueAt(qreal x) const;
};

QString timeText(int milliseconds);

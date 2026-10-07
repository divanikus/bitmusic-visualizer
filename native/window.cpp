#include "window.h"
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>
#include <algorithm>

QString timeText(int ms) {
    const int seconds = std::max(0, ms / 1000);
    return QString("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
int Timeline::valueAt(qreal x) const {
    QStyleOptionSlider option; initStyleOption(&option);
    const auto handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
    return QStyle::sliderValueFromPosition(minimum(), maximum(), qRound(x) - handle.width() / 2,
        std::max(1, width() - handle.width()), option.upsideDown);
}
void Timeline::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) { QSlider::mousePressEvent(event); return; }
    setFocus(); setSliderDown(true); setValue(valueAt(event->position().x())); event->accept();
}
void Timeline::mouseMoveEvent(QMouseEvent *event) {
    if (isSliderDown()) { setValue(valueAt(event->position().x())); event->accept(); }
    else QSlider::mouseMoveEvent(event);
}
void Timeline::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && isSliderDown()) {
        setValue(valueAt(event->position().x())); setSliderDown(false); event->accept();
    } else QSlider::mouseReleaseEvent(event);
}

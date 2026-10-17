#include "scopewindow.h"
#include <QCheckBox>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
class FullscreenButton : public QPushButton {
public:
    explicit FullscreenButton(bool exit = false, QWidget *parent = nullptr) : QPushButton(parent), exit_(exit) {
        setObjectName(exit ? "exitFullscreen" : "scopeFullscreen"); setFixedSize(30, 26);
        setToolTip(exit ? "Exit full screen (Esc / F11 / double-click visualization)"
            : "Full screen (F11 / double-click visualization outside edit mode)");
        setAccessibleName(toolTip()); setCursor(Qt::PointingHandCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen); p.setBrush(QColor(underMouse() || hasFocus() ? "#30434a" : "#202a30"));
        p.drawRoundedRect(rect(), 6, 6);
        p.setPen(QPen(QColor("#b8cccf"), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        for (int x : {-1, 1}) for (int y : {-1, 1}) {
            const QPointF corner(15+x*(exit_ ? 3 : 8), 13+y*(exit_ ? 2 : 7));
            const int direction = exit_ ? 1 : -1;
            p.drawLine(corner, corner+QPointF(x*direction*5, 0));
            p.drawLine(corner, corner+QPointF(0, y*direction*5));
        }
    }
private:
    bool exit_;
};
}

void ScopeWindow::setupFullscreen() {
    auto button = new FullscreenButton; titleBar_->addAction(button);
    connect(button, &QPushButton::clicked, this, &ScopeWindow::toggleFullscreen);
    fullscreenExit_ = new FullscreenButton(true, this); fullscreenExit_->hide();
    connect(fullscreenExit_, &QPushButton::clicked, this, &ScopeWindow::toggleFullscreen);
    fullscreenExitTimer_ = new QTimer(this); fullscreenExitTimer_->setSingleShot(true);
    connect(fullscreenExitTimer_, &QTimer::timeout, this, [this] {
        if (fullscreenExit_->underMouse()) fullscreenExitTimer_->start(1500);
        else fullscreenExit_->hide();
    });
}
void ScopeWindow::revealFullscreenExit() {
    if (!presentationFullscreen()) return;
    fullscreenExit_->show(); fullscreenExit_->raise(); fullscreenExitTimer_->start(1500);
}
QByteArray ScopeWindow::windowedGeometry() const {
    if (!fullscreenGeometry_.isEmpty()) return fullscreenGeometry_;
    if (!isFullScreen()) return saveGeometry();
    // A frameless maximized window may also be reported as native fullscreen
    // when the work area covers the monitor. Persist our windowed mode explicitly.
    QWidget placement(nullptr, Qt::Window | Qt::FramelessWindowHint);
    placement.setScreen(screen()); placement.setGeometry(isMaximized() ? normalGeometry() : geometry());
    if (isMaximized()) placement.setWindowState(Qt::WindowMaximized);
    return placement.saveGeometry();
}
void ScopeWindow::toggleFullscreen() {
    cancelDrag();
    if (fullscreenGeometry_.isEmpty()) {
        fullscreenGeometry_ = windowedGeometry(); fullscreenEditor_ = edit_->isChecked(); fullscreenMaximized_ = isMaximized();
        edit_->setChecked(false); titleBar_->hide(); showFullScreen();
        updateEditorGeometry(); revealFullscreenExit();
    } else {
        const auto geometry = fullscreenGeometry_; fullscreenGeometry_.clear();
        fullscreenExitTimer_->stop(); fullscreenExit_->hide();
        showNormal(); restoreGeometry(geometry);
        if (fullscreenMaximized_) showMaximized();
        titleBar_->show(); edit_->setChecked(fullscreenEditor_);
        updateEditorGeometry();
    }
    if (gpu_) gpu_->suspend();
    chromeKey_.clear(); update();
}

QRectF ScopeWindow::contentRect(int card) const {
    return panelRect(card).adjusted(12, edit_->isChecked() || names_->isChecked() || numbers_->isChecked() ? 32 : 10, -12, -10);
}
bool ScopeWindow::meterStereo(int) const { return state_.info.stereoOutput; }
QRectF ScopeWindow::meterRect(int card, int side) const {
    const auto content = contentRect(card);
    if (!waveOptions_.meters || content.width() < 100 || content.height() < 44) return {};
    const bool stereo = meterStereo(card);
    if (side && !stereo) return {};
    return {content.left()+side*11, content.top()+2, 6, content.height()-16};
}
QRectF ScopeWindow::plotRect(int card) const {
    auto plot = contentRect(card);
    if (!meterRect(card, 0).isEmpty()) plot.adjust(meterStereo(card) ? 30 : 19, 0, 0, 0);
    return plot;
}
void ScopeWindow::paintMeterTracks(QPainter &p, int card) {
    const auto &colors = theme_.colors(sourceFor(card));
    const bool stereo = meterStereo(card);
    p.save(); p.setFont(QFont("Segoe UI", 7));
    for (int side = 0; side < (stereo ? 2 : 1); ++side) {
        const auto track = meterRect(card, side); if (track.isEmpty()) continue;
        auto background = colors.axis; background.setAlpha(100); p.fillRect(track, background);
        p.setPen(colors.axis);
        for (int tick = 0; tick <= 5; ++tick) {
            const auto y = track.top()+track.height()*tick/5;
            p.drawLine(QPointF(track.left()-2, y), QPointF(track.right()+2, y));
        }
        auto label = colors.label;
        if (laneDimmed(sourceFor(card), stereo, side)) label.setAlpha(115);
        p.setPen(label);
        p.drawText(QRectF(track.left()-3, track.bottom()+2, 12, 12), Qt::AlignCenter, stereo ? (side ? "R" : "L") : "M");
    }
    p.restore();
}
QVector<ScopeMeter> ScopeWindow::meterFills() const {
    QVector<ScopeMeter> fills;
    if (!waveOptions_.meters || state_.busy) return fills;
    std::array<std::array<float, 2>, VoiceMask::Capacity+1> levels;
    levels.fill({-1, -1});
    for (int card : displayedChannels()) {
        if (!channelReady(card) || meterRect(card, 0).isEmpty()) continue;
        const int source = sourceFor(card);
        const bool stereo = meterStereo(card);
        for (int side = 0; side < (stereo ? 2 : 1); ++side) {
            auto &level = levels[source][side];
            if (level < 0) {
                const auto &samples = sourceSamples(source, stereo ? side : -1);
                level = scopeMeterLevel(samples);
            }
            if (level <= 0) continue;
            auto rect = meterRect(card, side); rect.setTop(rect.bottom()-rect.height()*level);
            auto color = theme_.colors(source).waveform;
            if (laneDimmed(source, stereo, side)) color.setAlpha(115);
            fills.push_back({rect, color, card});
        }
    }
    return fills;
}

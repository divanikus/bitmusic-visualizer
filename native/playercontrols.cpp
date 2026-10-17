#include "playercontrols.h"
#include "engine.h"
#include "playlist.h"
#include "shortcuts.h"
#include "timeline.h"
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
// Vector icons remain readable at different Windows display scales and need no icon font.
class IconButton : public QPushButton {
public:
    IconButton(PlayerIcon symbol, const QString &name, const QString &tip, QWidget *parent = nullptr)
        : QPushButton(parent), symbol_(symbol) {
        setObjectName(name); setToolTip(tip); setAccessibleName(tip); setFixedSize(34, 34);
        setCursor(Qt::PointingHandCursor);
    }
    void setSymbol(PlayerIcon symbol) { if (symbol_ != symbol) { symbol_ = symbol; update(); } }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const bool primary = objectName() == "primary";
        const QColor ink = !isEnabled() ? QColor("#647078") : primary ? QColor("#093a34")
            : isChecked() ? QColor("#9de9d8") : QColor("#dbe5e5");
        QColor fill = primary ? QColor("#a2e5d5") : isChecked() ? QColor("#294c48") : QColor("#202a30");
        if (underMouse() && isEnabled()) fill = fill.lighter(118);
        if (isDown()) fill = fill.darker(115);
        if (!isEnabled()) fill = QColor("#20282d");
        p.setPen(hasFocus() ? QPen(QColor("#93d9cb"), 1) : QPen(Qt::NoPen)); p.setBrush(fill);
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), primary ? 20 : 10, primary ? 20 : 10);
        p.translate(width() / 2.0 - 10, height() / 2.0 - 10);
        p.setPen(QPen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(ink);
        switch (symbol_) {
        case PlayerIcon::Play: p.setPen(Qt::NoPen); p.drawPolygon(QPolygonF{{6, 3}, {16, 10}, {6, 17}}); break;
        case PlayerIcon::Pause: p.setPen(Qt::NoPen); p.drawRoundedRect(QRectF(5, 3, 3.5, 14), 1, 1); p.drawRoundedRect(QRectF(12, 3, 3.5, 14), 1, 1); break;
        case PlayerIcon::Stop: p.setPen(Qt::NoPen); p.drawRoundedRect(QRectF(4, 4, 12, 12), 1.5, 1.5); break;
        case PlayerIcon::Previous: case PlayerIcon::Next:
            if (symbol_ == PlayerIcon::Previous) { p.translate(20, 0); p.scale(-1, 1); }
            p.setPen(Qt::NoPen); p.drawPolygon(QPolygonF{{4, 4}, {13, 10}, {4, 16}}); p.drawRoundedRect(QRectF(14, 4, 2, 12), .7, .7); break;
        case PlayerIcon::Repeat:
            p.setBrush(Qt::NoBrush); p.drawLine(4, 5, 17, 5); p.drawLine(17, 5, 14, 2); p.drawLine(17, 5, 14, 8);
            p.drawLine(16, 15, 3, 15); p.drawLine(3, 15, 6, 12); p.drawLine(3, 15, 6, 18);
            p.drawLine(3, 5, 3, 9); p.drawLine(17, 11, 17, 15);
            if (property("repeatOne").toBool()) { auto f=p.font(); f.setPixelSize(8); p.setFont(f); p.drawText(QRect(7,5,6,10),Qt::AlignCenter,"1"); } break;
        case PlayerIcon::Shuffle:
            p.setBrush(Qt::NoBrush); p.drawPolyline(QPolygonF{{2,4},{5,4},{14,16},{18,16}});
            p.drawPolyline(QPolygonF{{2,16},{5,16},{14,4},{18,4}});
            p.drawPolyline(QPolygonF{{15,1},{18,4},{15,7}});p.drawPolyline(QPolygonF{{15,13},{18,16},{15,19}});break;
        case PlayerIcon::Open: {
            p.setBrush(Qt::NoBrush); QPainterPath folder;
            folder.moveTo(2, 15); folder.lineTo(2, 4); folder.lineTo(8, 4); folder.lineTo(10, 6); folder.lineTo(17, 6); folder.lineTo(17, 9);
            folder.moveTo(2, 16); folder.lineTo(5, 9); folder.lineTo(19, 9); folder.lineTo(16, 16); folder.closeSubpath(); p.drawPath(folder); break;
        }
        case PlayerIcon::Scopes:
            p.setBrush(Qt::NoBrush); p.drawRoundedRect(QRectF(1, 2, 18, 16), 2, 2);
            p.drawPolyline(QPolygonF{{3, 10}, {6, 10}, {8, 5}, {11, 15}, {13, 9}, {17, 9}}); break;
        case PlayerIcon::Settings:
            p.setBrush(Qt::NoBrush);p.setPen(QPen(ink,1.8));p.drawEllipse(QRectF(4,4,12,12));p.drawEllipse(QRectF(8,8,4,4));
            p.translate(10,10);for(int i=0;i<8;++i) {p.drawLine(QPointF(0,-7),QPointF(0,-9));p.rotate(45);}break;
        case PlayerIcon::Volume:
            p.setPen(Qt::NoPen); p.drawPolygon(QPolygonF{{2, 8}, {6, 8}, {11, 4}, {11, 16}, {6, 12}, {2, 12}});
            p.setBrush(Qt::NoBrush); p.setPen(QPen(ink, 1.7)); p.drawArc(QRectF(10, 4, 8, 12), -65 * 16, 130 * 16); break;
        }
    }
private:
    PlayerIcon symbol_;
};
}
QPushButton *createPlayerIconButton(PlayerIcon icon, const QString &name, const QString &tip) {
    return new IconButton(icon, name, tip);
}
PlayerTransport::PlayerTransport(QVBoxLayout *root) : QObject(root->parentWidget()) {
    timeline_ = new Timeline; timeline_->setObjectName("timeline"); timeline_->setRange(0, unknownRangeMs_); root->addWidget(timeline_);
    auto times = new QHBoxLayout;
    clock_ = new QLabel("00:00"); clock_->setObjectName("clock"); total_ = new QLabel(QString::fromUtf8("—")); total_->setObjectName("muted"); times->addWidget(clock_); times->addStretch();
    leftOutput_ = new QToolButton; rightOutput_ = new QToolButton;
    int side = 0;
    for (auto button : {leftOutput_, rightOutput_}) {
        button->setObjectName(side ? "rightOutput" : "leftOutput"); button->setText(side ? "R" : "L");
        button->setAccessibleName(side ? "Right output" : "Left output");
        button->setCheckable(true); button->setChecked(true); button->setFixedSize(26, 22);
        button->setStyleSheet("QToolButton { padding: 0; border-radius: 5px; }");
        button->setCursor(Qt::PointingHandCursor); times->addWidget(button);
        connect(button, &QToolButton::toggled, this, [this](bool) {
            if (onOutputMask) onOutputMask((leftOutput_->isChecked() ? 1u : 0u) | (rightOutput_->isChecked() ? 2u : 0u));
        });
        ++side;
    }
    times->addSpacing(8); times->addWidget(total_); root->addLayout(times);
    auto transport = new QHBoxLayout; transport->setSpacing(4);
    previous_ = new IconButton(PlayerIcon::Previous, "previous", QString::fromUtf8("Previous track"));
    stop_ = new IconButton(PlayerIcon::Stop, "stop", QString::fromUtf8("Stop"));
    play_ = new IconButton(PlayerIcon::Play, "primary", QString::fromUtf8("Play / pause (Space)")); play_->setFixedSize(42, 42);
    next_ = new IconButton(PlayerIcon::Next, "next", QString::fromUtf8("Next track"));
    repeat_ = new IconButton(PlayerIcon::Repeat, "repeat", QString::fromUtf8("Repeat current track")); repeat_->setCheckable(true);
    shuffle_ = new IconButton(PlayerIcon::Shuffle, "shuffle", "Shuffle playlist"); shuffle_->setCheckable(true);
    for (auto button : {previous_, stop_, play_, next_, repeat_, shuffle_}) transport->addWidget(button);
    transport->addStretch();
    auto mute = new IconButton(PlayerIcon::Volume, "muteVolume", QString::fromUtf8("Mute / unmute")); transport->addWidget(mute);
    auto volume = new QSlider(Qt::Horizontal); volume->setObjectName("volume"); volume->setRange(0, 100); volume->setValue(65); volume->setFixedWidth(72); volume->setToolTip(QString::fromUtf8("Volume")); transport->addWidget(volume); root->addLayout(transport);
    connect(play_, &QPushButton::clicked, this, [this] { if (onPlayPause) onPlayPause(); });
    connect(stop_, &QPushButton::clicked, this, [this] { if (onStop) onStop(); });
    connect(previous_, &QPushButton::clicked, this, [this] { if (onPrevious) onPrevious(); });
    connect(next_, &QPushButton::clicked, this, [this] { if (onNext) onNext(); });
    connect(repeat_, &QPushButton::clicked, this, [this] { if (onRepeat) onRepeat(); });
    connect(shuffle_, &QPushButton::clicked, this, [this](bool on) { if (onShuffle) onShuffle(on); });
    connect(volume, &QSlider::valueChanged, this, [this](int value) { if (onVolume) onVolume(value / 100.f); });
    connect(mute, &QPushButton::clicked, this, [volume, savedVolume = 65]() mutable {
        if (volume->value()) { savedVolume = volume->value(); volume->setValue(0); } else volume->setValue(savedVolume);
    });
    connect(timeline_, &QSlider::sliderReleased, this, [this] { if (onSeek) onSeek(timeline_->value()); });
    connect(timeline_, &QSlider::sliderMoved, this, [this](int value) { clock_->setText(timeText(value)); });
    connect(timeline_, &QSlider::valueChanged, this, [this](int value) {
        if (!timeline_->isSliderDown() && timeline_->hasFocus() && onSeek) onSeek(value);
    });
}
void PlayerTransport::clickPlay() { play_->click(); }
void PlayerTransport::present(const PlayerState &s, const Playlist &playlist, PlaylistRepeat repeat) {
    const bool ready = s.valid && !s.busy;
    play_->setEnabled(!s.busy && (ready || !playlist.entries.isEmpty())); stop_->setEnabled(ready); previous_->setEnabled(!playlist.entries.isEmpty());
    next_->setEnabled(playlist.hasNext(repeat==PlaylistRepeat::All)); timeline_->setEnabled(ready);
    static_cast<IconButton *>(play_)->setSymbol(s.playing && !s.busy ? PlayerIcon::Pause : PlayerIcon::Play);
    repeat_->setEnabled(true);
    { QSignalBlocker block(repeat_); repeat_->setChecked(repeat!=PlaylistRepeat::Off); }
    const bool repeatOne=repeat==PlaylistRepeat::Track;
    if(repeat_->property("repeatOne").toBool()!=repeatOne) {repeat_->setProperty("repeatOne",repeatOne);repeat_->update();}
    { QSignalBlocker block(shuffle_); shuffle_->setChecked(playlist.shuffle); }
    shuffle_->setToolTip(playlist.shuffle?"Shuffle playlist: on":"Shuffle playlist: off");
    int side = 0;
    for (auto button : {leftOutput_, rightOutput_}) {
        const bool audible = (s.outputMask & (1u << side)) != 0;
        QSignalBlocker block(button); button->setChecked(audible); button->setEnabled(s.valid);
        button->setToolTip(QString("%1 output: %2\n%3: toggle output")
            .arg(side ? "Right" : "Left", audible ? "on (click to mute)" : "muted (click to enable)",
                shortcutLabel(Qt::ControlModifier, side ? Qt::Key_Right : Qt::Key_Left)));
        ++side;
    }
    repeat_->setToolTip(QString("Repeat: %1 (click to change)").arg(repeat==PlaylistRepeat::Off?"Off":repeat==PlaylistRepeat::Track?"Track":"Playlist")+
        (s.valid && s.playbackEndMs<0?"\nNo known end: use Next to advance.":""));
    repeat_->setAccessibleName(repeat_->toolTip());
    if (!timeline_->isSliderDown()) {
        if (s.positionMs > unknownRangeMs_ - 30000) unknownRangeMs_ = std::min(3600000, ((s.positionMs / 60000) + 2) * 60000);
        QSignalBlocker block(timeline_); timeline_->setRange(0, s.playbackEndMs > 0 ? std::min(3600000, s.playbackEndMs) : unknownRangeMs_); timeline_->setValue(s.positionMs); clock_->setText(timeText(s.positionMs));
    }
    total_->setText(s.playbackEndMs > 0 ? timeText(s.playbackEndMs) : QString::fromUtf8("—"));
    total_->setToolTip(s.durationLimited?"Playback limit for a track without duration":(s.info.durationMs<0 && s.playbackEndMs>=0)?"End or loop boundary reported by the decoder":(s.info.midiResources || s.info.mt32Resources) ? "MIDI sequence duration plus 2 seconds for release and effects" : s.info.configuration.family=="tracker" ? "Duration calculated from module patterns" : s.info.durationMs > 0 ? QString::fromUtf8("Duration from file") : QString::fromUtf8("No known end; playback continues until stopped"));
    timeline_->setToolTip(s.playbackEndMs > 0 ? QString::fromUtf8("Seek") : QString::fromUtf8("Seek: range expands during playback"));
}

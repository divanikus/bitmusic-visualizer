#include "playerchannels.h"
#include "engine.h"
#include "shortcuts.h"
#include <QGridLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStylePainter>
#include <QStyleOptionToolButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class ChannelButton : public QToolButton {
public:
    std::function<void()> onSolo;
    void setSolo(bool solo) {
        if (property("solo").toBool() == solo) return;
        setProperty("solo", solo); update();
    }
    void present(const PlayerState &state, int i) {
        const QSignalBlocker block(this);
        const auto instrument = state.instruments.value(i);
        const auto name = instrument.isEmpty() ? state.info.voices.value(i) : state.info.voices.value(i) + " · " + instrument;
        setText(QString("%1  %2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(name));
        QString tip = state.info.voices.value(i) + (instrument.isEmpty() ? QString() : " / " + instrument)
            + "\nPressed: channel is audible\nShift+click: Solo / restore previous channels";
        if (i < 9) tip += '\n' + channelShortcutHint(i);
        setToolTip(tip);
        setChecked(!(state.muteMask & (VoiceMask::bit(i)))); setEnabled(state.valid && !state.busy);
        setSolo(state.soloChannel == i);
        setAccessibleName(text() + (state.soloChannel == i ? "; Solo" : QString()));
    }
    QSize minimumSizeHint() const override { return {40, 32}; }
protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ShiftModifier && isDown()) {
            setDown(false);
            if (isEnabled() && rect().contains(event->position().toPoint()) && onSolo) onSolo();
            event->accept();
            return;
        }
        QToolButton::mouseReleaseEvent(event);
    }
    void paintEvent(QPaintEvent *) override {
        const bool solo = property("solo").toBool();
        QStyleOptionToolButton option; initStyleOption(&option);
        option.text = fontMetrics().elidedText(text(), Qt::ElideRight, std::max(0, width() - (solo ? 62 : 20)));
        const auto label = option.text;
        if (solo) option.text.clear();
        QStylePainter painter(this); painter.drawComplexControl(QStyle::CC_ToolButton, option);
        if (solo) {
            painter.setPen(QColor(isEnabled() ? "#b4eddf" : "#647078"));
            painter.drawText(rect().adjusted(10, 0, -51, 0), Qt::AlignLeft | Qt::AlignVCenter, label);
            const QRect badge(width() - 43, (height() - 18) / 2, 37, 18);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen); painter.setBrush(QColor("#f2c878")); painter.drawRoundedRect(badge, 4, 4);
            auto badgeFont = font(); badgeFont.setPointSizeF(7); badgeFont.setBold(true); painter.setFont(badgeFont);
            painter.setPen(QColor("#30250f")); painter.drawText(badge, Qt::AlignCenter, "SOLO");
        }
    }
};
}
PlayerChannels::PlayerChannels(QWidget *parent) : QWidget(parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8); layout->setSpacing(6);
    auto scroll = new QScrollArea; scroll->setWidgetResizable(true);
    auto contents = new QWidget; channels_ = new QGridLayout(contents);
    channels_->setContentsMargins(0, 0, 0, 0); channels_->setSpacing(6); channels_->setAlignment(Qt::AlignTop);
    scroll->setWidget(contents); layout->addWidget(scroll, 1);
    auto all = new QPushButton(QString::fromUtf8("Enable all"));
    auto none = new QPushButton(QString::fromUtf8("Disable all"));
    all->setObjectName("enableAllChannels"); none->setObjectName("disableAllChannels");
    auto actions = new QHBoxLayout; actions->addWidget(all); actions->addWidget(none); actions->addStretch();
    layout->addLayout(actions);
    connect(all, &QPushButton::clicked, this, [this] { if (onAllAudibleChanged) onAllAudibleChanged(true); });
    connect(none, &QPushButton::clicked, this, [this] { if (onAllAudibleChanged) onAllAudibleChanged(false); });
}
int PlayerChannels::count() const { return channels_->count(); }
void PlayerChannels::rebuild(const PlayerState &state) {
    while (auto item = channels_->takeAt(0)) { delete item->widget(); delete item; }
    for (int i = 0; i < state.info.voices.size(); ++i) {
        auto button = new ChannelButton;
        button->setObjectName(QString("voice%1").arg(i)); button->setCheckable(true);
        button->setMinimumHeight(32); button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        channels_->addWidget(button, i / 2, i % 2);
        button->present(state, i);
        button->onSolo = [this, i] { if (onSoloRequested) onSoloRequested(i); };
        connect(button, &QToolButton::toggled, this, [this, i](bool audible) {
            if (onAudibleChanged) onAudibleChanged(i, audible);
        });
    }
}
void PlayerChannels::present(const PlayerState &state) {
    for (int i = 0; i < channels_->count(); ++i)
        static_cast<ChannelButton *>(channels_->itemAt(i)->widget())->present(state, i);
}

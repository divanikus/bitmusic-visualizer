#include "window.h"
#include "appsettings.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QStylePainter>
#include <QStyleOptionToolButton>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
enum class Symbol { Previous, Stop, Play, Pause, Next, Repeat, Open, Scopes, Volume };
class ChannelButton : public QToolButton {
public:
    QSize minimumSizeHint() const override { return {40, 32}; }
protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionToolButton option; initStyleOption(&option);
        option.text = fontMetrics().elidedText(text(), Qt::ElideRight, std::max(0, width() - 20));
        QStylePainter painter(this); painter.drawComplexControl(QStyle::CC_ToolButton, option);
    }
};
// Vector icons remain readable at different Windows display scales and need no icon font.
class IconButton : public QPushButton {
public:
    IconButton(Symbol symbol, const QString &name, const QString &tip, QWidget *parent = nullptr)
        : QPushButton(parent), symbol_(symbol) {
        setObjectName(name); setToolTip(tip); setAccessibleName(tip); setFixedSize(34, 34);
        setCursor(Qt::PointingHandCursor);
    }
    void setSymbol(Symbol symbol) { if (symbol_ != symbol) { symbol_ = symbol; update(); } }
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
        case Symbol::Play: p.setPen(Qt::NoPen); p.drawPolygon(QPolygonF{{6, 3}, {16, 10}, {6, 17}}); break;
        case Symbol::Pause: p.setPen(Qt::NoPen); p.drawRoundedRect(QRectF(5, 3, 3.5, 14), 1, 1); p.drawRoundedRect(QRectF(12, 3, 3.5, 14), 1, 1); break;
        case Symbol::Stop: p.setPen(Qt::NoPen); p.drawRoundedRect(QRectF(4, 4, 12, 12), 1.5, 1.5); break;
        case Symbol::Previous: case Symbol::Next:
            if (symbol_ == Symbol::Previous) { p.translate(20, 0); p.scale(-1, 1); }
            p.setPen(Qt::NoPen); p.drawPolygon(QPolygonF{{4, 4}, {13, 10}, {4, 16}}); p.drawRoundedRect(QRectF(14, 4, 2, 12), .7, .7); break;
        case Symbol::Repeat:
            p.setBrush(Qt::NoBrush); p.drawLine(4, 5, 17, 5); p.drawLine(17, 5, 14, 2); p.drawLine(17, 5, 14, 8);
            p.drawLine(16, 15, 3, 15); p.drawLine(3, 15, 6, 12); p.drawLine(3, 15, 6, 18);
            p.drawLine(3, 5, 3, 9); p.drawLine(17, 11, 17, 15); break;
        case Symbol::Open: {
            p.setBrush(Qt::NoBrush); QPainterPath folder;
            folder.moveTo(2, 15); folder.lineTo(2, 4); folder.lineTo(8, 4); folder.lineTo(10, 6); folder.lineTo(17, 6); folder.lineTo(17, 9);
            folder.moveTo(2, 16); folder.lineTo(5, 9); folder.lineTo(19, 9); folder.lineTo(16, 16); folder.closeSubpath(); p.drawPath(folder); break;
        }
        case Symbol::Scopes:
            p.setBrush(Qt::NoBrush); p.drawRoundedRect(QRectF(1, 2, 18, 16), 2, 2);
            p.drawPolyline(QPolygonF{{3, 10}, {6, 10}, {8, 5}, {11, 15}, {13, 9}, {17, 9}}); break;
        case Symbol::Volume:
            p.setPen(Qt::NoPen); p.drawPolygon(QPolygonF{{2, 8}, {6, 8}, {11, 4}, {11, 16}, {6, 12}, {2, 12}});
            p.setBrush(Qt::NoBrush); p.setPen(QPen(ink, 1.7)); p.drawArc(QRectF(10, 4, 8, 12), -65 * 16, 130 * 16); break;
        }
    }
private:
    Symbol symbol_;
};

class TrackDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {180, 30}; }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        p->save(); p->setRenderHint(QPainter::Antialiasing);
        const bool current = index.data(Qt::UserRole + 1).toBool();
        if (option.state & QStyle::State_Selected) p->fillRect(option.rect, QColor("#263f3f"));
        else if (option.state & QStyle::State_MouseOver) p->fillRect(option.rect, QColor("#222f35"));
        const auto r = option.rect.adjusted(10, 0, -10, 0);
        p->setFont(option.font); p->setPen(current ? QColor("#a2e5d5") : QColor("#8f9eaa"));
        if (index.data(Qt::UserRole + 2).toBool()) {
            p->setPen(Qt::NoPen); p->setBrush(QColor("#a2e5d5"));
            p->drawPolygon(QPolygonF{{qreal(r.left() + 4), qreal(r.center().y() - 4)},
                {qreal(r.left() + 11), qreal(r.center().y())}, {qreal(r.left() + 4), qreal(r.center().y() + 4)}});
        } else p->drawText(QRect(r.left(), r.top(), 25, r.height()), Qt::AlignVCenter, QString::number(index.row() + 1));
        p->setPen(current ? QColor("#c3f1e7") : QColor("#dce5e8"));
        const QRect title(r.left() + 28, r.top(), std::max(0, r.width() - 86), r.height());
        p->drawText(title, Qt::AlignVCenter, option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, title.width()));
        p->setPen(QColor("#92a6b0"));
        p->drawText(QRect(r.right() - 52, r.top(), 52, r.height()), Qt::AlignRight | Qt::AlignVCenter, index.data(Qt::UserRole).toString());
        p->restore();
    }
};
}

PlayerWindow::PlayerWindow() {
    setWindowTitle("Bit Music"); setAcceptDrops(true); resize(430, 430); setMinimumSize(380, 330);
    setStyleSheet(R"(
      QWidget { background: #171e24; color: #dce5e8; font-family: 'Segoe UI'; font-size: 9pt; }
      QLabel#title { font-size: 12pt; font-weight: 600; color: #e6eeef; }
      QLabel#muted, QLabel#status { color: #92a6b0; font-size: 8pt; }
      QLabel#clock { color: #a2e5d5; font-family: Consolas; }
      QPushButton, QToolButton { background: #253239; border: none; border-radius: 8px; padding: 6px 10px; }
      QPushButton:hover, QToolButton:hover { background: #32444b; }
      QToolButton:checked { background: #294c48; color: #b4eddf; }
      QPushButton:disabled, QToolButton:disabled { color: #647078; }
      QTabWidget::pane { border: 1px solid #314047; border-radius: 8px; top: -1px; background: #1b252b; }
      QTabBar::tab { background: transparent; padding: 9px 18px; color: #92a6b0; border-bottom: 2px solid transparent; }
      QTabBar::tab:selected { color: #a2e5d5; border-bottom-color: #a2e5d5; }
      QTabBar::tab:hover { background: #222f35; }
      QListWidget { background: #1b252b; border: none; border-radius: 8px; padding: 4px 0; outline: none; }
      QScrollArea { border: none; background: #1b252b; }
      QSlider { background: transparent; min-height: 18px; }
      QSlider::groove:horizontal { background: #3b4c53; height: 4px; border-radius: 2px; }
      QSlider::sub-page:horizontal { background: #a2e5d5; border-radius: 2px; }
      QSlider::handle:horizontal { background: #a2e5d5; width: 12px; margin: -4px 0; border-radius: 6px; }
      QScrollBar:vertical { background: transparent; width: 7px; margin: 3px 0; }
      QScrollBar::handle:vertical { background: #46575e; border-radius: 3px; min-height: 24px; }
      QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
      QToolTip { color: #ecf2f3; background: #304048; border: none; padding: 5px; }
    )");
    auto root = new QVBoxLayout(this); root->setContentsMargins(14, 6, 14, 12); root->setSpacing(6);
    auto titleBar = new TitleBar(this); titleBar->setTitle(QApplication::applicationName()); root->addWidget(titleBar);
    auto header = new QHBoxLayout; header->setSpacing(8);
    auto text = new QVBoxLayout; text->setSpacing(2);
    title_ = new QLabel(QString::fromUtf8("Open a file")); title_->setObjectName("title"); title_->setMinimumWidth(0); title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    subtitle_ = new QLabel("NSF · VGM · SPC · GBS · AY"); subtitle_->setObjectName("muted"); subtitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    text->addWidget(title_); text->addWidget(subtitle_); header->addLayout(text, 1);
    auto open = new IconButton(Symbol::Open, "open", QString::fromUtf8("Open file (Ctrl+O)"));
    showScopes_ = new IconButton(Symbol::Scopes, "showScopes", QString::fromUtf8("Scopes")); showScopes_->setCheckable(true);
    header->addWidget(open); header->addWidget(showScopes_); root->addLayout(header);
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
            player_.setOutputMask((leftOutput_->isChecked() ? 1u : 0u) | (rightOutput_->isChecked() ? 2u : 0u));
            refresh();
        });
        ++side;
    }
    times->addSpacing(8); times->addWidget(total_); root->addLayout(times);
    auto transport = new QHBoxLayout; transport->setSpacing(4);
    previous_ = new IconButton(Symbol::Previous, "previous", QString::fromUtf8("Previous track"));
    stop_ = new IconButton(Symbol::Stop, "stop", QString::fromUtf8("Stop"));
    play_ = new IconButton(Symbol::Play, "primary", QString::fromUtf8("Play / pause (Space)")); play_->setFixedSize(42, 42);
    next_ = new IconButton(Symbol::Next, "next", QString::fromUtf8("Next track"));
    repeat_ = new IconButton(Symbol::Repeat, "repeat", QString::fromUtf8("Repeat current track")); repeat_->setCheckable(true);
    for (auto button : {previous_, stop_, play_, next_, repeat_}) transport->addWidget(button);
    transport->addStretch();
    auto mute = new IconButton(Symbol::Volume, "muteVolume", QString::fromUtf8("Mute / unmute")); transport->addWidget(mute);
    auto volume = new QSlider(Qt::Horizontal); volume->setObjectName("volume"); volume->setRange(0, 100); volume->setValue(65); volume->setFixedWidth(72); volume->setToolTip(QString::fromUtf8("Volume")); transport->addWidget(volume); root->addLayout(transport);
    tabs_ = new QTabWidget; tabs_->setObjectName("tabs");
    songs_ = new QListWidget; songs_->setObjectName("songs"); songs_->setItemDelegate(new TrackDelegate(songs_)); songs_->setMouseTracking(true); songs_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); songs_->setSelectionMode(QAbstractItemView::SingleSelection);
    tabs_->addTab(songs_, QString::fromUtf8("Tracks"));
    auto channelPage = new QWidget; auto channelLayout = new QVBoxLayout(channelPage); channelLayout->setContentsMargins(8, 8, 8, 8); channelLayout->setSpacing(6);
    auto scroll = new QScrollArea; scroll->setWidgetResizable(true); auto channelContents = new QWidget; channels_ = new QGridLayout(channelContents); channels_->setContentsMargins(0, 0, 0, 0); channels_->setSpacing(6); channels_->setAlignment(Qt::AlignTop); scroll->setWidget(channelContents); channelLayout->addWidget(scroll, 1);
    auto all = new QPushButton(QString::fromUtf8("Enable all")); auto none = new QPushButton(QString::fromUtf8("Disable all")); auto channelActions = new QHBoxLayout; channelActions->addWidget(all); channelActions->addWidget(none); channelActions->addStretch(); channelLayout->addLayout(channelActions);
    tabs_->addTab(channelPage, QString::fromUtf8("Channels")); root->addWidget(tabs_, 1);
    status_ = new QLabel; status_->setObjectName("status"); status_->setWordWrap(true); status_->hide(); root->addWidget(status_);
    connect(open, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QString::fromUtf8("Open music"), {}, "Chiptunes (*.nsf *.nsfe *.vgm *.vgz *.spc *.gbs *.ay);;All files (*)");
        if (!path.isEmpty()) loadFile(path);
    });
    connect(showScopes_, &QPushButton::clicked, this, [this] { setScopesVisible(!scopes_.renderingVisible()); });
    connect(play_, &QPushButton::clicked, this, [this] {
        const auto s = player_.state();
        if (!s.playing && songs_->currentRow() >= 0 && songs_->currentRow() != s.info.song) playSong(songs_->currentRow());
        else player_.seek(s.info.durationMs > 0 && s.positionMs >= s.info.durationMs ? 0 : s.positionMs, !s.playing);
    });
    connect(stop_, &QPushButton::clicked, this, [this] { player_.seek(0, false); });
    connect(previous_, &QPushButton::clicked, this, [this] { auto s = player_.state(); playSong(std::max(0, s.info.song - 1)); });
    connect(next_, &QPushButton::clicked, this, [this] { auto s = player_.state(); if (s.info.song + 1 < s.info.songs) playSong(s.info.song + 1); });
    connect(repeat_, &QPushButton::toggled, this, [this](bool on) { player_.setRepeat(on); });
    connect(volume, &QSlider::valueChanged, this, [this](int value) { player_.setVolume(value / 100.f); });
    connect(mute, &QPushButton::clicked, this, [volume, savedVolume = 65]() mutable {
        if (volume->value()) { savedVolume = volume->value(); volume->setValue(0); } else volume->setValue(savedVolume);
    });
    connect(all, &QPushButton::clicked, this, [this] { player_.setMuteMask(0); });
    connect(none, &QPushButton::clicked, this, [this] { player_.setMuteMask(0xffffffffu); });
    connect(songs_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { playSong(songs_->row(item)); });
    connect(timeline_, &QSlider::sliderReleased, this, [this] { player_.seek(timeline_->value(), player_.state().playing); });
    connect(timeline_, &QSlider::sliderMoved, this, [this](int value) { clock_->setText(timeText(value)); });
    connect(timeline_, &QSlider::valueChanged, this, [this](int value) { if (!timeline_->isSliderDown() && timeline_->hasFocus()) player_.seek(value, player_.state().playing); });
    auto openShortcut = new QShortcut(QKeySequence::Open, this); connect(openShortcut, &QShortcut::activated, open, &QPushButton::click);
    auto playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this); connect(playShortcut, &QShortcut::activated, play_, &QPushButton::click);
    scopes_.onVisibilityChanged = [this](bool visible) {
        player_.setScopesEnabled(visible);
        player_.setOutputScopesEnabled(visible && scopes_.outputEnabled());
        QSignalBlocker block(showScopes_); showScopes_->setChecked(visible);
        showScopes_->setToolTip(visible ? QString::fromUtf8("Hide scopes") : QString::fromUtf8("Show scopes"));
    };
    scopes_.onChannelsChanged = [this](uint32_t mask) { player_.setScopeMask(mask); };
    scopes_.onOutputChanged = [this](bool enabled) { player_.setOutputScopesEnabled(enabled && scopes_.renderingVisible()); };
    scopes_.onRefreshRequested = [this] { refreshScopes(); };
    scopes_.onFrameRateChanged = [this](int fps) { player_.setScopeFrameRate(fps); };
    player_.setScopeFrameRate(scopes_.frameRate());
    QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
    restoreGeometry(settings.value("Windows/Player").toByteArray());
    auto timer = new QTimer(this); timer->setInterval(33);
    connect(timer, &QTimer::timeout, this, [this] {
        refreshControls();
        // Keep file/channel metadata current while capture and painting are asleep.
        if (!scopes_.renderingVisible()) refreshScopes();
    });
    timer->start(); refresh();
}
PlayerWindow::~PlayerWindow() {
    scopes_.onVisibilityChanged = {}; scopes_.onChannelsChanged = {}; scopes_.onOutputChanged = {};
    scopes_.onRefreshRequested = {}; scopes_.onFrameRateChanged = {}; player_.shutdown();
}
void PlayerWindow::setScopesVisible(bool visible) {
    if (visible) {
        if (scopes_.isMinimized()) scopes_.setWindowState(scopes_.windowState() & ~Qt::WindowMinimized);
        scopes_.show(); scopes_.raise();
    } else scopes_.hide();
}
void PlayerWindow::loadFile(const QString &path, int song) {
    player_.load(path, song);
    scopes_.beginFile();
    // Open with the first file, then respect the user's visibility choice across tracks/files.
    if (!openedOnce_) { openedOnce_ = true; setScopesVisible(true); }
}
void PlayerWindow::playSong(int song) { player_.load(player_.state().info.path, song); }
void PlayerWindow::closeEvent(QCloseEvent *event) {
    // Snapshot before hiding, including a scope window hidden earlier by the user.
    QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
    settings.setValue("Windows/Player", saveGeometry());
    settings.setValue("Windows/Scopes", scopes_.saveGeometry());
    settings.sync();
    scopes_.hide(); scopes_.onVisibilityChanged = {}; player_.shutdown();
    if (settings.status() != QSettings::NoError)
        QMessageBox::warning(this, "Could not save settings",
            "Window positions and sizes could not be saved to:\n" + playerSettingsPath() +
            "\n\nPlease check that this settings location is writable.");
    event->accept();
}
void PlayerWindow::dragEnterEvent(QDragEnterEvent *event) { if (event->mimeData()->hasUrls() && event->mimeData()->urls().first().isLocalFile()) event->acceptProposedAction(); }
void PlayerWindow::dropEvent(QDropEvent *event) { if (event->mimeData()->hasUrls()) loadFile(event->mimeData()->urls().first().toLocalFile()); }
void PlayerWindow::rebuildChannels(const PlayerState &state) {
    while (auto item = channels_->takeAt(0)) { delete item->widget(); delete item; }
    for (int i = 0; i < state.info.voices.size(); ++i) {
        auto button = new ChannelButton; button->setText(QString("%1  %2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(state.info.voices[i]));
        button->setObjectName(QString("voice%1").arg(i)); button->setCheckable(true); button->setChecked(true); button->setMinimumHeight(32); button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setToolTip(state.info.voices[i] + "\nPressed: channel is audible"); channels_->addWidget(button, i / 2, i % 2);
        connect(button, &QToolButton::toggled, this, [this, i](bool audible) {
            auto mask = player_.state().muteMask; if (audible) mask &= ~(uint32_t(1) << i); else mask |= uint32_t(1) << i; player_.setMuteMask(mask);
        });
    }
}
void PlayerWindow::refresh() {
    refreshControls(); refreshScopes();
}
void PlayerWindow::refreshControls() {
    const auto s = player_.state();
    const bool changed = s.valid && (shownPath_ != s.info.path || shownSong_ != s.info.song);
    if (changed) {
        const bool fileChanged = shownPath_ != s.info.path;
        shownPath_ = s.info.path; shownSong_ = s.info.song; unknownRangeMs_ = 180000;
        if (fileChanged || songs_->count() != s.info.playlist.size()) {
            songs_->clear();
            for (const auto &song : s.info.playlist) {
                auto item = new QListWidgetItem(song.title, songs_); item->setData(Qt::UserRole, song.durationMs > 0 ? timeText(song.durationMs) : QString::fromUtf8("—"));
                item->setToolTip(song.title + (song.durationMs > 0 ? " · " + timeText(song.durationMs) : QString::fromUtf8(" · Duration not provided")));
            }
            tabs_->setTabText(0, QString::fromUtf8("Tracks (%1)").arg(s.info.songs)); rebuildChannels(s);
        }
        songs_->setCurrentRow(s.info.song); songs_->scrollToItem(songs_->item(s.info.song));
    }
    if (s.valid) {
        title_->setText(title_->fontMetrics().elidedText(s.info.title, Qt::ElideRight, title_->width())); title_->setToolTip(s.info.title);
        const auto subtitle = QFileInfo(s.info.path).fileName() + " · " + s.info.system;
        subtitle_->setText(subtitle_->fontMetrics().elidedText(subtitle, Qt::ElideMiddle, subtitle_->width())); subtitle_->setToolTip(subtitle);
    }
    const bool ready = s.valid && !s.busy;
    play_->setEnabled(ready); stop_->setEnabled(ready); previous_->setEnabled(ready); next_->setEnabled(ready && s.info.song + 1 < s.info.songs); timeline_->setEnabled(ready); songs_->setEnabled(ready);
    static_cast<IconButton *>(play_)->setSymbol(s.playing && !s.busy ? Symbol::Pause : Symbol::Play);
    repeat_->setEnabled(ready && s.info.durationMs > 0);
    { QSignalBlocker block(repeat_); repeat_->setChecked(s.repeat); }
    int side = 0;
    for (auto button : {leftOutput_, rightOutput_}) {
        const bool audible = (s.outputMask & (1u << side)) != 0;
        QSignalBlocker block(button); button->setChecked(audible); button->setEnabled(s.valid);
        button->setToolTip(QString("%1 output: %2").arg(side ? "Right" : "Left", audible ? "on (click to mute)" : "muted (click to enable)"));
        ++side;
    }
    repeat_->setToolTip(s.info.durationMs > 0 ? QString::fromUtf8("Repeat current track: ") + (s.repeat ? QString::fromUtf8("on") : QString::fromUtf8("off"))
        : QString::fromUtf8("No track endpoint provided; playback continues indefinitely"));
    if (!timeline_->isSliderDown()) {
        if (s.positionMs > unknownRangeMs_ - 30000) unknownRangeMs_ = std::min(3600000, ((s.positionMs / 60000) + 2) * 60000);
        QSignalBlocker block(timeline_); timeline_->setRange(0, s.info.durationMs > 0 ? std::min(3600000, s.info.durationMs) : unknownRangeMs_); timeline_->setValue(s.positionMs); clock_->setText(timeText(s.positionMs));
    }
    total_->setText(s.info.durationMs > 0 ? timeText(s.info.durationMs) : QString::fromUtf8("—"));
    total_->setToolTip(s.info.durationMs > 0 ? QString::fromUtf8("Duration from file") : QString::fromUtf8("No duration provided; playback continues indefinitely"));
    timeline_->setToolTip(s.info.durationMs > 0 ? QString::fromUtf8("Seek") : QString::fromUtf8("Seek: range expands during playback"));
    for (int i = 0; i < songs_->count(); ++i) {
        auto item = songs_->item(i);
        if (item->data(Qt::UserRole + 1).toBool() != (s.valid && i == s.info.song)) item->setData(Qt::UserRole + 1, s.valid && i == s.info.song);
        const bool playing = s.valid && !s.busy && s.playing && i == s.info.song;
        if (item->data(Qt::UserRole + 2).toBool() != playing) item->setData(Qt::UserRole + 2, playing);
    }
    for (int i = 0; i < channels_->count(); ++i) { auto button = qobject_cast<QToolButton *>(channels_->itemAt(i)->widget()); QSignalBlocker block(button); button->setChecked(!(s.muteMask & (uint32_t(1) << i))); button->setEnabled(ready); }
    const auto message = !s.error.isEmpty() ? s.error : !s.scopeError.isEmpty() ? s.scopeError : s.busy ? QString::fromUtf8("Loading…") : QString();
    status_->setText(message); status_->setVisible(!message.isEmpty());
}
void PlayerWindow::refreshScopes() {
    scopes_.present(player_.state(), scopes_.renderingVisible() ? player_.scopes() : ScopeFrame{},
                    scopes_.renderingVisible() && scopes_.outputEnabled() ? player_.outputScopes() : OutputFrame{});
}

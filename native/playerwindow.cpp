#include "window.h"
#include "shortcuts.h"
#include "appsettings.h"
#include "miditrack.h"
#include "mt32track.h"
#include <QDesktopServices>
#include <QComboBox>
#include <QFile>
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
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSettings>
#include <QStylePainter>
#include <QStyleOptionToolButton>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
enum class Symbol { Previous, Stop, Play, Pause, Next, Repeat, Shuffle, Open, Scopes, Volume };
class ChannelButton : public QToolButton {
public:
    std::function<void()> onSolo;
    void setSolo(bool solo) {
        if (property("solo").toBool() == solo) return;
        setProperty("solo", solo); update();
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
            p.drawLine(3, 5, 3, 9); p.drawLine(17, 11, 17, 15);
            if (property("repeatOne").toBool()) { auto f=p.font(); f.setPixelSize(8); p.setFont(f); p.drawText(QRect(7,5,6,10),Qt::AlignCenter,"1"); } break;
        case Symbol::Shuffle:
            p.setBrush(Qt::NoBrush); p.drawPolyline(QPolygonF{{2,4},{5,4},{14,16},{18,16}});
            p.drawPolyline(QPolygonF{{2,16},{5,16},{14,4},{18,4}});
            p.drawPolyline(QPolygonF{{15,1},{18,4},{15,7}});p.drawPolyline(QPolygonF{{15,13},{18,16},{15,19}});break;
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
}

PlayerWindow::PlayerWindow() {
    setWindowTitle("Bit Music"); setAcceptDrops(true); resize(430, 430); setMinimumSize(400, 330);
    player_.setAutoSubsong(false);
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
    setupShortcutsHelp(titleBar);
    auto header = new QHBoxLayout; header->setSpacing(8);
    auto text = new QVBoxLayout; text->setSpacing(2);
    title_ = new QLabel(QString::fromUtf8("Open a file")); title_->setObjectName("title"); title_->setMinimumWidth(0); title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    subtitle_ = new QLabel("Open music with Ctrl+O or drop a file"); subtitle_->setObjectName("muted"); subtitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    subtitle_->setTextFormat(Qt::PlainText);
    text->addWidget(title_); text->addWidget(subtitle_); header->addLayout(text, 1);
    auto open = new IconButton(Symbol::Open, "open", QString::fromUtf8("Open music or playlist (Ctrl+O)"));
    showScopes_ = new IconButton(Symbol::Scopes, "showScopes", QString::fromUtf8("Visualizations")); showScopes_->setCheckable(true);
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
    shuffle_ = new IconButton(Symbol::Shuffle, "shuffle", "Shuffle playlist"); shuffle_->setCheckable(true);
    for (auto button : {previous_, stop_, play_, next_, repeat_, shuffle_}) transport->addWidget(button);
    transport->addStretch();
    auto mute = new IconButton(Symbol::Volume, "muteVolume", QString::fromUtf8("Mute / unmute")); transport->addWidget(mute);
    auto volume = new QSlider(Qt::Horizontal); volume->setObjectName("volume"); volume->setRange(0, 100); volume->setValue(65); volume->setFixedWidth(72); volume->setToolTip(QString::fromUtf8("Volume")); transport->addWidget(volume); root->addLayout(transport);
    tabs_ = new QTabWidget; tabs_->setObjectName("tabs");
    setupPlaylistUi();
    auto channelPage = new QWidget; auto channelLayout = new QVBoxLayout(channelPage); channelLayout->setContentsMargins(8, 8, 8, 8); channelLayout->setSpacing(6);
    auto scroll = new QScrollArea; scroll->setWidgetResizable(true); auto channelContents = new QWidget; channels_ = new QGridLayout(channelContents); channels_->setContentsMargins(0, 0, 0, 0); channels_->setSpacing(6); channels_->setAlignment(Qt::AlignTop); scroll->setWidget(channelContents); channelLayout->addWidget(scroll, 1);
    auto all = new QPushButton(QString::fromUtf8("Enable all")); auto none = new QPushButton(QString::fromUtf8("Disable all"));
    all->setObjectName("enableAllChannels"); none->setObjectName("disableAllChannels");
    auto channelActions = new QHBoxLayout; channelActions->addWidget(all); channelActions->addWidget(none); channelActions->addStretch(); channelLayout->addLayout(channelActions);
    tabs_->addTab(channelPage, QString::fromUtf8("Channels")); root->addWidget(tabs_, 1);
    midiControls_=new QWidget; midiControls_->setObjectName("midiControls");
    auto midiRow=new QHBoxLayout(midiControls_); midiRow->setContentsMargins(0,0,0,0);
    midiEngine_=new QComboBox; midiEngine_->setObjectName("midiEngine");
    midiEngine_->addItem("Auto","auto"); midiEngine_->addItem("General MIDI","gm"); midiEngine_->addItem("MT-32","mt32");
    midiEngine_->setCurrentIndex(midiEngine_->findData(selectedMidiEngine()));
    midiEngine_->setToolTip("MIDI engine. Auto recognizes MT-32 SysEx; select MT-32 manually for arrangements without it. Changing the engine restarts playback.");
    midiRow->addWidget(midiEngine_);
    connect(midiEngine_,&QComboBox::activated,this,[this](int index) {
        if(index<0 || midiEngine_->itemData(index).toString()==selectedMidiEngine()) return;
        try { selectMidiEngine(midiEngine_->itemData(index).toString()); if(!midiPath_.isEmpty()) startSource(midiPath_, 0); }
        catch(const std::exception &e) { QMessageBox::warning(this,"MIDI engine",QString::fromUtf8(e.what())); }
    });
    soundFont_=new QComboBox; soundFont_->setObjectName("soundFont");
    soundFont_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    soundFont_->setMinimumContentsLength(12);
    soundFont_->setToolTip("General MIDI bank. Changing it restarts the current MIDI.");
    midiRow->addWidget(soundFont_,1);
    auto bankFolder=new IconButton(Symbol::Open,"soundFontFolder","Open SoundFonts folder"); midiRow->addWidget(bankFolder);
    auto bankRefresh=new QToolButton; bankRefresh->setText(QString::fromUtf8("↻"));
    bankRefresh->setObjectName("refreshSoundFonts"); bankRefresh->setToolTip("Refresh SoundFont list"); midiRow->addWidget(bankRefresh);
    root->addWidget(midiControls_); midiControls_->hide();
    connect(bankFolder,&QPushButton::clicked,this,[this] {
        const auto s=player_.state();
        const bool mt32=midiEngine_->currentData()=="mt32" || (midiEngine_->currentData()=="auto" && ((s.valid && s.info.mt32Resources) || s.error.contains("MT-32")));
        const auto dir=mt32?playerMt32Directory():playerSoundFontsDirectory();
        QDir().mkpath(dir); QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(bankRefresh,&QToolButton::clicked,this,[this] { refreshSoundFonts(); });
    connect(soundFont_,&QComboBox::activated,this,[this](int index) {
        const auto name=soundFont_->itemData(index).toString();
        if(name.isEmpty() || name==selectedSoundFont()) return;
        try { selectSoundFont(name); if(!midiPath_.isEmpty()) startSource(midiPath_, 0); }
        catch(const std::exception &e) { QMessageBox::warning(this,"SoundFont",QString::fromUtf8(e.what())); }
    });
    status_ = new QLabel; status_->setObjectName("status"); status_->setWordWrap(true); status_->hide(); root->addWidget(status_);
    connect(open, &QPushButton::clicked, this, [this] { chooseMusic(true); });
    connect(showScopes_, &QPushButton::clicked, this, [this] { setScopesVisible(!scopes_.renderingVisible()); });
    connect(play_, &QPushButton::clicked, this, [this] {
        const auto s = player_.state();
        if (songs_->currentRow() >= 0 && (!s.valid || (!s.playing && songs_->currentRow() != playlist_.currentRow()))) playSong(songs_->currentRow());
        else player_.seek(s.info.durationMs > 0 && s.positionMs >= s.info.durationMs ? 0 : s.positionMs, !s.playing);
    });
    connect(stop_, &QPushButton::clicked, this, [this] { player_.seek(0, false); });
    connect(previous_, &QPushButton::clicked, this, [this] { stepPlaylist(false); });
    connect(next_, &QPushButton::clicked, this, [this] { stepPlaylist(true); });
    connect(repeat_, &QPushButton::clicked, this, [this] { setPlaylistRepeat(PlaylistRepeat((int(playlistRepeat_)+1)%3)); refresh(); });
    connect(shuffle_, &QPushButton::clicked, this, [this](bool on) { setPlaylistShuffle(on); refresh(); });
    connect(volume, &QSlider::valueChanged, this, [this](int value) { player_.setVolume(value / 100.f); });
    connect(mute, &QPushButton::clicked, this, [volume, savedVolume = 65]() mutable {
        if (volume->value()) { savedVolume = volume->value(); volume->setValue(0); } else volume->setValue(savedVolume);
    });
    connect(all, &QPushButton::clicked, this, [this] { player_.setMuteMask(0); });
    connect(none, &QPushButton::clicked, this, [this] { player_.setMuteMask(~VoiceMask{}); });
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
        showScopes_->setToolTip(visible ? QString::fromUtf8("Hide visualizations") : QString::fromUtf8("Show visualizations"));
    };
    scopes_.onChannelsChanged = [this](VoiceMask mask) { player_.setScopeMask(mask); };
    scopes_.onSoloRequested = [this](int source) { player_.toggleSolo(source); refresh(); };
    scopes_.onOutputChanged = [this](bool enabled) { player_.setOutputScopesEnabled(enabled && scopes_.renderingVisible()); };
    scopes_.onRefreshRequested = [this] { refreshScopes(); };
    scopes_.onFrameRateChanged = [this](int fps) { player_.setScopeFrameRate(fps); };
    player_.setScopeFrameRate(scopes_.frameRate());
    QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
    restoreGeometry(settings.value("Windows/Player").toByteArray());
    playlistRepeat_=PlaylistRepeat(std::clamp(settings.value("Playback/Repeat",0).toInt(),0,2));
    playlist_.setShuffle(settings.value("Playback/Shuffle",false).toBool());
    auto timer = new QTimer(this); timer->setInterval(33);
    connect(timer, &QTimer::timeout, this, [this] {
        refreshControls();
        // Keep file/channel metadata current while capture and painting are asleep.
        if (!scopes_.renderingVisible()) refreshScopes();
    });
    timer->start(); refresh();
}
PlayerWindow::~PlayerWindow() {
    scopes_.onSoloRequested = {};
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
    if(isPlaylistFile(path)) {addFiles({path},true);return;}
    playlistLoader_.cancel();playlist_.clear();replacingPlaylist_=false;playlistPath_.clear();playlistMessage_.clear();rebuildPlaylist();
    pendingSingle_=true;pendingSinglePath_=path;
    if(player_.state().valid && player_.state().info.path==path) scopes_.beginFile();
    startSource(path,song);pendingSingleGeneration_=player_.state().generation;
}
void PlayerWindow::startSource(const QString &path,int song) {
    QFile input(path); const bool midi=input.open(QIODevice::ReadOnly) && input.read(4)=="MThd";
    midiPath_=midi?path:QString(); midiControls_->setVisible(midi);
    if(midi) refreshSoundFonts();
    const auto before=player_.state();
    player_.load(path, song);
    if(pendingSingle_ && path==pendingSinglePath_) pendingSingleGeneration_=player_.state().generation;
    if(!before.valid || before.info.path!=path) scopes_.beginFile();
    // Open with the first file, then respect the user's visibility choice across tracks/files.
    if (!openedOnce_) { openedOnce_ = true; setScopesVisible(true); }
}
void PlayerWindow::refreshSoundFonts() {
    const QSignalBlocker blocked(soundFont_); soundFont_->clear();
    const auto selected=selectedSoundFont();
    for(const auto &name:availableSoundFonts()) soundFont_->addItem(QFileInfo(name).completeBaseName(),name);
    int at=soundFont_->findData(selected);
    if(at<0) { soundFont_->insertItem(0,selected.isEmpty()?"No SoundFonts found":"Missing: "+selected,QString()); at=0; }
    soundFont_->setCurrentIndex(at);
}
void PlayerWindow::playSong(int song) {
    if(song<0 || song>=playlist_.entries.size())return;
    if(replacingPlaylist_) {playlistLoader_.cancel();replacingPlaylist_=false;}
    pendingSingle_=false;playlist_.activate(song);
    const auto entry=playlist_.entries[song]; startSource(entry.path,entry.song);
    songs_->setCurrentRow(song); songs_->scrollToItem(songs_->item(song));
}
void PlayerWindow::closeEvent(QCloseEvent *event) {
    // Snapshot before hiding, including a scope window hidden earlier by the user.
    QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
    settings.setValue("Playback/Repeat",int(playlistRepeat_));
    settings.setValue("Playback/Shuffle",playlist_.shuffle);
    settings.setValue("Windows/Player", saveGeometry());
    settings.setValue("Windows/Scopes", scopes_.windowedGeometry());
    settings.sync();
    scopes_.hide(); scopes_.onVisibilityChanged = {}; player_.shutdown();
    if (settings.status() != QSettings::NoError)
        QMessageBox::warning(this, "Could not save settings",
            "Window positions and sizes could not be saved to:\n" + playerSettingsPath() +
            "\n\nPlease check that this settings location is writable.");
    event->accept();
}
void PlayerWindow::dragEnterEvent(QDragEnterEvent *event) { if (event->mimeData()->hasUrls() && event->mimeData()->urls().first().isLocalFile()) event->acceptProposedAction(); }
void PlayerWindow::dropEvent(QDropEvent *event) {
    QStringList paths;for(const auto &url:event->mimeData()->urls())if(url.isLocalFile())paths<<url.toLocalFile();
    if(!paths.isEmpty()){addFiles(paths);event->acceptProposedAction();}
}
void PlayerWindow::rebuildChannels(const PlayerState &state) {
    while (auto item = channels_->takeAt(0)) { delete item->widget(); delete item; }
    for (int i = 0; i < state.info.voices.size(); ++i) {
        auto button = new ChannelButton; button->setText(QString("%1  %2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(state.info.voices[i]));
        button->setObjectName(QString("voice%1").arg(i)); button->setCheckable(true); button->setChecked(true); button->setMinimumHeight(32); button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setToolTip(state.info.voices[i] + "\nPressed: channel is audible\nShift+click: Solo / restore previous channels"); channels_->addWidget(button, i / 2, i % 2);
        if (i < 9) button->setToolTip(button->toolTip() + '\n' + channelShortcutHint(i));
        button->onSolo = [this, i] { player_.toggleSolo(i); refresh(); };
        connect(button, &QToolButton::toggled, this, [this, i](bool audible) {
            auto mask = player_.state().muteMask; if (audible) mask &= ~(VoiceMask::bit(i)); else mask |= VoiceMask::bit(i); player_.setMuteMask(mask);
        });
    }
}
void PlayerWindow::refresh() {
    refreshControls(); refreshScopes();
}
void PlayerWindow::refreshControls() {
    pollPlaylist();
    const auto s = player_.state();
    const bool changed = s.valid && (shownPath_ != s.info.path || shownSong_ != s.info.song);
    if (changed) {
        const bool fileChanged = shownPath_ != s.info.path;
        shownPath_ = s.info.path; shownSong_ = s.info.song; unknownRangeMs_ = 180000;
        if (fileChanged || channels_->count()!=s.info.voices.size()) rebuildChannels(s);
        player_.setRepeat(playlistRepeat_==PlaylistRepeat::Track && s.info.durationMs>0);
    }
    if (s.valid) {
        title_->setText(title_->fontMetrics().elidedText(s.info.title, Qt::ElideRight, title_->width()));
        title_->setToolTip(QString("<qt>%1<br>%2</qt>").arg(s.info.title.toHtmlEscaped(), QFileInfo(s.info.path).fileName().toHtmlEscaped()));
        const auto sound = s.info.configuration.name.isEmpty() ? QString("Unknown sound") : s.info.configuration.name;
        const auto subtitle = s.info.system.isEmpty() ? sound : s.info.system + " · " + sound;
        subtitle_->setText(subtitle_->fontMetrics().elidedText(subtitle, Qt::ElideRight, subtitle_->width()));
        subtitle_->setAccessibleName(subtitle);
    }
    if (!s.valid && !s.busy && playlist_.entries.isEmpty()) {title_->setText("Open music");title_->setToolTip({});subtitle_->clear();}
    const bool ready = s.valid && !s.busy;
    play_->setEnabled(!s.busy && (ready || !playlist_.entries.isEmpty())); stop_->setEnabled(ready); previous_->setEnabled(!playlist_.entries.isEmpty());
    next_->setEnabled(playlist_.hasNext(playlistRepeat_==PlaylistRepeat::All)); timeline_->setEnabled(ready); songs_->setEnabled(true);
    static_cast<IconButton *>(play_)->setSymbol(s.playing && !s.busy ? Symbol::Pause : Symbol::Play);
    repeat_->setEnabled(true);
    { QSignalBlocker block(repeat_); repeat_->setChecked(playlistRepeat_!=PlaylistRepeat::Off); }
    const bool repeatOne=playlistRepeat_==PlaylistRepeat::Track;
    if(repeat_->property("repeatOne").toBool()!=repeatOne) {repeat_->setProperty("repeatOne",repeatOne);repeat_->update();}
    { QSignalBlocker block(shuffle_); shuffle_->setChecked(playlist_.shuffle); }
    shuffle_->setToolTip(playlist_.shuffle?"Shuffle playlist: on":"Shuffle playlist: off");
    int side = 0;
    for (auto button : {leftOutput_, rightOutput_}) {
        const bool audible = (s.outputMask & (1u << side)) != 0;
        QSignalBlocker block(button); button->setChecked(audible); button->setEnabled(s.valid);
        button->setToolTip(QString("%1 output: %2\n%3: toggle output")
            .arg(side ? "Right" : "Left", audible ? "on (click to mute)" : "muted (click to enable)",
                shortcutLabel(Qt::ControlModifier, side ? Qt::Key_Right : Qt::Key_Left)));
        ++side;
    }
    repeat_->setToolTip(QString("Repeat: %1 (click to change)").arg(playlistRepeat_==PlaylistRepeat::Off?"Off":playlistRepeat_==PlaylistRepeat::Track?"Track":"Playlist")+
        (s.valid && s.info.durationMs<0?"\nNo duration provided: use Next to advance.":""));
    repeat_->setAccessibleName(repeat_->toolTip());
    if (!timeline_->isSliderDown()) {
        if (s.positionMs > unknownRangeMs_ - 30000) unknownRangeMs_ = std::min(3600000, ((s.positionMs / 60000) + 2) * 60000);
        QSignalBlocker block(timeline_); timeline_->setRange(0, s.info.durationMs > 0 ? std::min(3600000, s.info.durationMs) : unknownRangeMs_); timeline_->setValue(s.positionMs); clock_->setText(timeText(s.positionMs));
    }
    total_->setText(s.info.durationMs > 0 ? timeText(s.info.durationMs) : QString::fromUtf8("—"));
    const bool mt32=midiEngine_->currentData()=="mt32" || (midiEngine_->currentData()=="auto" && ((s.valid && s.info.mt32Resources) || s.error.contains("MT-32")));
    soundFont_->setVisible(!mt32);
    findChild<QToolButton *>("refreshSoundFonts")->setVisible(!mt32);
    findChild<QPushButton *>("soundFontFolder")->setToolTip(mt32?"Open MT-32 ROM folder":"Open SoundFonts folder");
    total_->setToolTip((s.info.midiResources || s.info.mt32Resources) ? "MIDI sequence duration plus 2 seconds for release and effects" : s.info.configuration.family=="tracker" ? "Duration calculated from module patterns" : s.info.durationMs > 0 ? QString::fromUtf8("Duration from file") : QString::fromUtf8("No duration provided; playback continues indefinitely"));
    timeline_->setToolTip(s.info.durationMs > 0 ? QString::fromUtf8("Seek") : QString::fromUtf8("Seek: range expands during playback"));
    for (int i = 0; i < songs_->count(); ++i) {
        auto item = songs_->item(i);
        const bool current=s.valid && item->data(Qt::UserRole+4).toULongLong()==playlist_.current;
        if (item->data(Qt::UserRole + 1).toBool()!=current) item->setData(Qt::UserRole+1,current);
        const bool playing = current && !s.busy && s.playing;
        if (item->data(Qt::UserRole + 2).toBool() != playing) item->setData(Qt::UserRole + 2, playing);
    }
    for (int i = 0; i < channels_->count(); ++i) {
        auto button = static_cast<ChannelButton *>(channels_->itemAt(i)->widget()); QSignalBlocker block(button);
        const auto instrument = s.instruments.value(i);
        const auto name = instrument.isEmpty() ? s.info.voices.value(i) : s.info.voices.value(i) + " · " + instrument;
        button->setText(QString("%1  %2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(name));
        QString tip = s.info.voices.value(i) + (instrument.isEmpty() ? QString() : " / " + instrument)
            + "\nPressed: channel is audible\nShift+click: Solo / restore previous channels";
        if (i < 9) tip += '\n' + channelShortcutHint(i);
        button->setToolTip(tip);
        button->setChecked(!(s.muteMask & (VoiceMask::bit(i)))); button->setEnabled(ready);
        button->setSolo(s.soloChannel == i);
        button->setAccessibleName(button->text() + (s.soloChannel == i ? "; Solo" : QString()));
    }
    const auto message = !s.error.isEmpty() ? s.error : !s.scopeError.isEmpty() ? s.scopeError : s.busy ? QString::fromUtf8("Loading…") : playlistMessage_;
    status_->setText(message.section('\n', 0, 0)); status_->setToolTip(message); status_->setVisible(!message.isEmpty());
    if ((s.error.startsWith("C64 ROM required:") || s.error.startsWith("YM2608 ROM required:") || s.error.startsWith("SoundFont required:") || s.error.startsWith("MT-32 ROM required:")) && romWarningGeneration_ != s.generation) {
        romWarningGeneration_ = s.generation;
        auto warning = new QMessageBox(QMessageBox::Warning, s.error.startsWith("SoundFont required:") ? "SoundFont required" : "ROM required", s.error, QMessageBox::Ok, this);
        warning->setObjectName("romWarning"); warning->setTextFormat(Qt::PlainText);
        warning->setAttribute(Qt::WA_DeleteOnClose); warning->open();
    }
}
void PlayerWindow::refreshScopes() {
    scopes_.present(player_.state(), scopes_.renderingVisible() ? player_.scopes() : ScopeFrame{},
                    scopes_.renderingVisible() && scopes_.outputEnabled() ? player_.outputScopes() : OutputFrame{});
}

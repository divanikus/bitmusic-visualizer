#include "playerwindow.h"
#include "playercontrols.h"
#include "playlistview.h"
#include "shortcuts.h"
#include "appsettings.h"
#include "playersettings.h"
#include <QDialog>
#include "miditrack.h"
#include "mt32track.h"
#include <QDesktopServices>
#include <QComboBox>
#include <QFile>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
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
    setupShortcuts();
    auto settingsButton=createPlayerIconButton(PlayerIcon::Settings,"playerSettingsButton","Settings");
    connect(settingsButton,&QPushButton::clicked,this,[this] {showSettings();});
    auto header = new QHBoxLayout; header->setSpacing(8);
    auto text = new QVBoxLayout; text->setSpacing(2);
    title_ = new QLabel(QString::fromUtf8("Open a file")); title_->setObjectName("title"); title_->setMinimumWidth(0); title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    subtitle_ = new QLabel("Open music with Ctrl+O or drop a file"); subtitle_->setObjectName("muted"); subtitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    subtitle_->setTextFormat(Qt::PlainText);
    text->addWidget(title_); text->addWidget(subtitle_); header->addLayout(text, 1);
    auto open = createPlayerIconButton(PlayerIcon::Open, "open", QString::fromUtf8("Open music or playlist (Ctrl+O)"));
    showScopes_ = createPlayerIconButton(PlayerIcon::Scopes, "showScopes", QString::fromUtf8("Visualizations")); showScopes_->setCheckable(true);
    header->addWidget(open); header->addWidget(showScopes_); header->addWidget(settingsButton); root->addLayout(header);
    transport_ = new PlayerTransport(root);
    transport_->onPlayPause = [this] { togglePlayback(); };
    transport_->onStop = [this] { player_.seek(0, false); };
    transport_->onPrevious = [this] { stepPlaylist(false); };
    transport_->onNext = [this] { stepPlaylist(true); };
    transport_->onRepeat = [this] { setPlaylistRepeat(PlaylistRepeat((int(playlistRepeat_) + 1) % 3)); refresh(); };
    transport_->onShuffle = [this](bool on) { setPlaylistShuffle(on); refresh(); };
    transport_->onVolume = [this](float volume) { player_.setVolume(volume); };
    transport_->onOutputMask = [this](uint32_t mask) { player_.setOutputMask(mask); refresh(); };
    transport_->onSeek = [this](int position) { player_.seek(position, player_.state().playing); };
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
    auto bankFolder=createPlayerIconButton(PlayerIcon::Open,"soundFontFolder","Open SoundFonts folder"); midiRow->addWidget(bankFolder);
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
    connect(all, &QPushButton::clicked, this, [this] { player_.setMuteMask(0); });
    connect(none, &QPushButton::clicked, this, [this] { player_.setMuteMask(~VoiceMask{}); });
    connect(songs_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { playSong(songs_->row(item)); });
    auto openShortcut = new QShortcut(QKeySequence::Open, this); connect(openShortcut, &QShortcut::activated, open, &QPushButton::click);
    auto playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this); connect(playShortcut, &QShortcut::activated, transport_, &PlayerTransport::clickPlay);
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
    player_.setUnknownDurationLimit(unknownTrackLimitPreference());
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
void PlayerWindow::startSource(const QString &path,int song) {
    QFile input(path); const bool midi=input.open(QIODevice::ReadOnly) && input.read(4)=="MThd";
    midiPath_=midi?path:QString(); midiControls_->setVisible(midi);
    if(midi) refreshSoundFonts();
    const auto before=player_.state();
    player_.load(path, song);
    if(pendingSingle_ && path==pendingSingle_->path) pendingSingle_->generation=player_.state().generation;
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
void PlayerWindow::closeEvent(QCloseEvent *event) {
    if(auto dialog=findChild<QDialog *>("playerSettings"))dialog->close();
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
void PlayerWindow::togglePlayback() {
    const auto state = player_.state();
    const int selected = songs_->currentRow();
    if (selected >= 0 && (!state.valid || (!state.playing && selected != playlist_.currentRow()))) playSong(selected);
    else player_.seek(state.info.durationMs > 0 && state.positionMs >= state.info.durationMs ? 0 : state.positionMs, !state.playing);
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
        shownPath_ = s.info.path; shownSong_ = s.info.song; transport_->resetTimelineRange();
        if (fileChanged || channels_->count()!=s.info.voices.size()) rebuildChannels(s);
        player_.setRepeat(playlistRepeat_==PlaylistRepeat::Track);
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
    transport_->present(s, playlist_, playlistRepeat_);
    songs_->setEnabled(true);
    const bool ready = s.valid && !s.busy;
    const bool mt32=midiEngine_->currentData()=="mt32" || (midiEngine_->currentData()=="auto" && ((s.valid && s.info.mt32Resources) || s.error.contains("MT-32")));
    soundFont_->setVisible(!mt32);
    findChild<QToolButton *>("refreshSoundFonts")->setVisible(!mt32);
    findChild<QPushButton *>("soundFontFolder")->setToolTip(mt32?"Open MT-32 ROM folder":"Open SoundFonts folder");
    songs_->presentPlayback(s.valid ? playlist_.current : 0, !s.busy && s.playing);
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

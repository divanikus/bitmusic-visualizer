#include "playerwindow.h"
#include "playercontrols.h"
#include "playerchannels.h"
#include "midicontrols.h"
#include "playlistview.h"
#include "appsettings.h"
#include "playersettings.h"
#include "resourceerror.h"
#include <QDialog>
#include <QDesktopServices>
#include <QDir>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

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
    channels_ = new PlayerChannels;
    channels_->onAudibleChanged = [this](int source, bool audible) {
        auto mask = player_.state().muteMask;
        if (audible) mask &= ~VoiceMask::bit(source); else mask |= VoiceMask::bit(source);
        player_.setMuteMask(mask);
    };
    channels_->onSoloRequested = [this](int source) { player_.toggleSolo(source); refresh(); };
    channels_->onAllAudibleChanged = [this](bool audible) { player_.setMuteMask(audible ? VoiceMask{} : ~VoiceMask{}); };
    tabs_->addTab(channels_, QString::fromUtf8("Channels")); root->addWidget(tabs_, 1);
    midiControls_ = new MidiControls;
    midiControls_->onReloadRequested = [this](const QString &path) { startSource(path, 0); };
    midiControls_->onFolderRequested = [this] {
        const auto dir = midiControls_->resourceDirectory(player_.state());
        QDir().mkpath(dir); QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    };
    root->addWidget(midiControls_);
    status_ = new QLabel; status_->setObjectName("status"); status_->setWordWrap(true); status_->hide(); root->addWidget(status_);
    connect(open, &QPushButton::clicked, this, [this] { chooseMusic(true); });
    connect(showScopes_, &QPushButton::clicked, this, [this] { setScopesVisible(!scopes_.renderingVisible()); });
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
    // scopes_ is a top-level value member; detach callbacks capturing this before teardown.
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
    midiControls_->prepareSource(path);
    const auto before=player_.state();
    player_.load(path, song);
    if(pendingSingle_ && path==pendingSingle_->path) pendingSingle_->generation=player_.state().generation;
    if(!before.valid || before.info.path!=path) scopes_.beginFile();
    // Open with the first file, then respect the user's visibility choice across tracks/files.
    if (!openedOnce_) { openedOnce_ = true; setScopesVisible(true); }
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
        if (fileChanged || channels_->count()!=s.info.voices.size()) channels_->rebuild(s);
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
    midiControls_->present(s);
    songs_->presentPlayback(s.valid ? playlist_.current : 0, !s.busy && s.playing);
    channels_->present(s);
    const auto message = !s.error.isEmpty() ? s.error : !s.scopeError.isEmpty() ? s.scopeError : s.busy ? QString::fromUtf8("Loading…") : playlistMessage_;
    status_->setText(message.section('\n', 0, 0)); status_->setToolTip(message); status_->setVisible(!message.isEmpty());
    showResourceWarning(s);
}
void PlayerWindow::showResourceWarning(const PlayerState &state) {
    const auto title = resourceWarningTitle(state.error);
    if (title.isEmpty() || resourceWarningGeneration_ == state.generation) return;
    resourceWarningGeneration_ = state.generation;
    auto warning = new QMessageBox(QMessageBox::Warning, title, state.error, QMessageBox::Ok, this);
    warning->setObjectName("romWarning"); warning->setTextFormat(Qt::PlainText);
    warning->setAttribute(Qt::WA_DeleteOnClose); warning->open();
}
void PlayerWindow::refreshScopes() {
    scopes_.present(player_.state(), scopes_.renderingVisible() ? player_.scopes() : ScopeFrame{},
                    scopes_.renderingVisible() && scopes_.outputEnabled() ? player_.outputScopes() : OutputFrame{});
}

void PlayerWindow::showSettings() {
    if (auto existing = findChild<QDialog *>("playerSettings")) {
        existing->show(); existing->raise(); existing->activateWindow(); return;
    }
    auto dialog = new PlayerSettingsDialog(scopes_.frameRate(), scopes_.rendererName(), this);
    dialog->onUnknownDurationLimitChanged = [this](int ms) { player_.setUnknownDurationLimit(ms); };
    dialog->onFrameRateChanged = [this](int fps) { scopes_.setFrameRate(fps); scopes_.saveFrameRate(); };
    dialog->onResourcesRefreshed = [this] { midiControls_->refreshSoundFonts(); };
    dialog->refreshResources();
    dialog->show();
}

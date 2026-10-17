#pragma once
#include "engine.h"
#include "playlist.h"
#include "chrome.h"
#include "scopewindow.h"
#include <optional>

class QLabel;
class QPushButton;
class PlaylistView;
class PlayerTransport;
class QTabWidget;
class MidiControls;
class PlayerChannels;

class PlayerWindow : public FrameWindow {
public:
    PlayerWindow();
    ~PlayerWindow() override;
    void loadFile(const QString &path, int song = 0);
    void addFiles(const QStringList &paths, bool replace = false);
    void savePlaylist(const QString &path);
    const Playlist &playlist() const { return playlist_; }
    void setPlaylistRepeat(PlaylistRepeat mode);
    void setPlaylistShuffle(bool enabled);
    NativePlayer &player() { return player_; }
    ScopeWindow &scopeWindow() { return scopes_; }
    void refresh();
    void showSettings();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
private:
    void refreshControls();
    void showResourceWarning(const PlayerState &state);
    void togglePlayback();
    void refreshScopes();
    void setScopesVisible(bool visible);
    void playSong(int song);
    void setupShortcuts();
    void setupPlaylistUi();
    void pollPlaylist();
    void applyPlaylistImports();
    void expandSingleFile(const PlayerState &state);
    void updatePlaylistMetadata(const PlayerState &state);
    void rebuildPlaylist();
    void decoratePlaylist();
    void removePlaylistSelection();
    void clearPlaylist();
    void chooseMusic(bool replace);
    void startSource(const QString &path, int song);
    void stepPlaylist(bool forward);
    Playlist playlist_;
    PlaylistLoader playlistLoader_;
    PlaylistRepeat playlistRepeat_ = PlaylistRepeat::Off;
    struct PendingSingleFile {
        QString path;
        quint64 generation = 0;
    };
    std::optional<PendingSingleFile> pendingSingle_;
    bool replacingPlaylist_ = false;
    QString playlistPath_, playlistMessage_;
    quint64 completedGeneration_ = 0;
    QLabel *playlistStatus_ = nullptr;
    NativePlayer player_;
    ScopeWindow scopes_;
    QLabel *title_, *subtitle_, *status_;
    QPushButton *showScopes_;
    PlayerTransport *transport_;
    PlaylistView *songs_;
    QTabWidget *tabs_;
    MidiControls *midiControls_;
    PlayerChannels *channels_;
    QString shownPath_;
    int shownSong_ = -1;
    int requestedSong_ = 0; // Preserve a pending/failed MIDI subsong when changing its engine or bank.
    bool openedOnce_ = false;
    quint64 resourceWarningGeneration_ = 0;
};

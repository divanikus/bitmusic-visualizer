#pragma once
#include "engine.h"
#include "playlist.h"
#include "chrome.h"
#include "scopewindow.h"
#include "timeline.h"

class QLabel;
class QPushButton;
class QToolButton;
class QListWidget;
class QTabWidget;
class QComboBox;
class QGridLayout;

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
    void refreshScopes();
    void rebuildChannels(const PlayerState &state);
    void setScopesVisible(bool visible);
    void playSong(int song);
    void setupShortcuts();
    void refreshSoundFonts();
    void setupPlaylistUi();
    void pollPlaylist();
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
    bool pendingSingle_ = false, replacingPlaylist_ = false;
    QString pendingSinglePath_, playlistPath_, playlistMessage_;
    quint64 pendingSingleGeneration_ = 0, completedGeneration_ = 0;
    QPushButton *shuffle_ = nullptr;
    QLabel *playlistStatus_ = nullptr;
    NativePlayer player_;
    ScopeWindow scopes_;
    QLabel *title_, *subtitle_, *clock_, *total_, *status_;
    QPushButton *play_, *stop_, *previous_, *next_, *repeat_, *showScopes_;
    QToolButton *leftOutput_, *rightOutput_;
    Timeline *timeline_;
    QListWidget *songs_;
    QTabWidget *tabs_;
    QWidget *midiControls_;
    QComboBox *soundFont_, *midiEngine_;
    QString midiPath_;
    QGridLayout *channels_;
    QString shownPath_;
    int shownSong_ = -1;
    bool openedOnce_ = false;
    quint64 romWarningGeneration_ = 0;
    int unknownRangeMs_ = 180000;
};

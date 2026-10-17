#pragma once
#include <QObject>
#include <QString>
#include <cstdint>
#include <functional>

class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class Timeline;
class Playlist;
struct PlayerState;
enum class PlaylistRepeat;

enum class PlayerIcon { Previous, Stop, Play, Pause, Next, Repeat, Shuffle, Open, Scopes, Volume, Settings };
QPushButton *createPlayerIconButton(PlayerIcon icon, const QString &name, const QString &tip);

// Builds the existing rows in the window layout, without adding a widget layer.
// Presents snapshots and reports user requests; never calls the audio engine.
class PlayerTransport : public QObject {
public:
    explicit PlayerTransport(QVBoxLayout *layout);
    void present(const PlayerState &state, const Playlist &playlist, PlaylistRepeat repeat);
    void resetTimelineRange() { unknownRangeMs_ = 180000; }
    void clickPlay();
    std::function<void()> onPlayPause, onStop, onPrevious, onNext, onRepeat;
    std::function<void(bool)> onShuffle;
    std::function<void(float)> onVolume;
    std::function<void(uint32_t)> onOutputMask;
    std::function<void(int)> onSeek;
private:
    QLabel *clock_, *total_;
    QPushButton *play_, *stop_, *previous_, *next_, *repeat_, *shuffle_;
    QToolButton *leftOutput_, *rightOutput_;
    Timeline *timeline_;
    int unknownRangeMs_ = 180000;
};

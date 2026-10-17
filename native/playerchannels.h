#pragma once
#include <QWidget>
#include <functional>

class QGridLayout;
struct PlayerState;

// Presents channel controls without changing playback or source identities.
class PlayerChannels : public QWidget {
public:
    explicit PlayerChannels(QWidget *parent = nullptr);
    int count() const;
    void rebuild(const PlayerState &state);
    void present(const PlayerState &state);
    std::function<void(int, bool)> onAudibleChanged;
    std::function<void(int)> onSoloRequested;
    std::function<void(bool)> onAllAudibleChanged;
private:
    QGridLayout *channels_;
};

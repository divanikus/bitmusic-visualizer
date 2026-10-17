#pragma once
#include <QWidget>
#include <functional>

class QComboBox;
class QPushButton;
class QToolButton;
struct PlayerState;

// MIDI resource selection and presentation; playback reloads remain with the player.
class MidiControls : public QWidget {
public:
    explicit MidiControls(QWidget *parent = nullptr);
    void prepareSource(const QString &path);
    void clearSource();
    void refreshSoundFonts();
    void present(const PlayerState &state);
    QString resourceDirectory(const PlayerState &state) const;
    std::function<void(const QString &)> onReloadRequested;
    std::function<void()> onFolderRequested;
private:
    bool usingMt32(const PlayerState &state) const;
    QComboBox *engine_, *soundFont_;
    QPushButton *folder_;
    QToolButton *refresh_;
    QString midiPath_;
};

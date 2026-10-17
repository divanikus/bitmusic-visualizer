#pragma once
#include <QDialog>
#include <functional>

class QTreeWidget;
class QVBoxLayout;

// Zero means no time limit. This preference only applies to unknown-duration tracks.
int unknownTrackLimitPreference();

// Settings pages and persistence, without access to playback or visualization.
class PlayerSettingsDialog : public QDialog {
public:
    PlayerSettingsDialog(int frameRate, const QString &rendererName, QWidget *parent = nullptr);
    // Bind owner callbacks before the initial table refresh.
    void refreshResources();
    std::function<void(int)> onUnknownDurationLimitChanged;
    std::function<void(int)> onFrameRateChanged;
    std::function<void()> onResourcesRefreshed;
private:
    void setupPlayback(QVBoxLayout *layout);
    void setupRendering(QVBoxLayout *layout, int frameRate, const QString &rendererName);
    void setupResources(QVBoxLayout *layout);
    void openResourceFolder();
    QTreeWidget *resourceFiles_;
};

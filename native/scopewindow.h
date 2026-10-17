#pragma once
#include "engine.h"
#include "chrome.h"
#include "scopetheme.h"
#include "scopegpu.h"
#include "scopewave.h"
#include "scopegrid.h"
#include "scopecards.h"
#include <QHash>
#include <QElapsedTimer>

class QLabel;
class QComboBox;
class QPushButton;
class QListWidget;
class QTabWidget;
class QGridLayout;
class QCheckBox;
class QSpinBox;
class QTimer;
class QChronoTimer;
class QDialog;
class QVBoxLayout;
class ScopeCardInspector;

class ScopeWindow : public FrameWindow {
public:
    ScopeWindow();
    using ViewKind = ScopeView;
    void cycleSourceView(int source);
    void toggleSourceVisibility(int source);
    void setAllViews(ViewKind kind);
    void resetLayout();
    void toggleFullscreen();
    bool presentationFullscreen() const { return !fullscreenGeometry_.isEmpty(); }
    QByteArray windowedGeometry() const;
    void present(const PlayerState &state, const ScopeFrame &frame, const OutputFrame &output = {});
    void beginFile();
    std::function<void(bool)> onVisibilityChanged;
    std::function<void(VoiceMask)> onChannelsChanged;
    std::function<void(int)> onSoloRequested;
    std::function<void(bool)> onOutputChanged;
    std::function<void()> onRefreshRequested;
    std::function<void(int)> onFrameRateChanged;
    int frameRate() const { return frameRate_; }
    void setFrameRate(int fps);
    void saveFrameRate() { rememberWaves(); }
    QString rendererName() const;
    bool outputEnabled() const;
    bool renderingVisible() const { return isVisible() && !isMinimized(); }
    QVector<int> visibleChannels() const;
    QRectF cardRect(int slot) const;
    QRectF panelRect(int channel) const;
protected:
    bool event(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void changeEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    void paintContents(QPainter &p, bool waves);
    QVector<ScopeLane> waveLanes();
    QByteArray chromeKey(bool dynamic = true) const;
    void setupFullscreen();
    void revealFullscreenExit();
    QByteArray fullscreenGeometry_;
    bool fullscreenEditor_ = false, fullscreenMaximized_ = false;
    QPushButton *fullscreenExit_ = nullptr;
    QTimer *fullscreenExitTimer_ = nullptr;
    QRectF contentRect(int card) const;
    QRectF plotRect(int card) const;
    QRectF meterRect(int card, int side) const;
    bool meterStereo(int card) const;
    void paintMeterTracks(QPainter &p, int card);
    QVector<ScopeMeter> meterFills() const;
    ScopeCards cards_;
    int sourceFor(int card) const { return cards_.sourceFor(card); }
    ViewKind viewFor(int card) const { return cards_.viewFor(card); }
    QString sourceName(int source) const;
    QString viewName(ViewKind kind) const;
    bool viewSupported(ViewKind kind) const;
    void paintMusic(QPainter &p, int card, const QRectF &plot);
    void paintPianoRoll(QPainter &p, int card, const QRectF &plot);
    void paintPattern(QPainter &p, int card, const QRectF &plot);
    QVector<ScopeOverlay> musicOverlays();
    struct MusicImage { QByteArray key; QImage image; };
    QHash<int, MusicImage> musicImages_;
    int selectedCardId() const;
    void updateViewEditor();
    void updateKeyboardNotes();
    QVector<float> keyboardPitches(int card) const;
    void addCard();
    void paintKeyboard(QPainter &p, int card, const QRectF &plot, bool muted);
    void paintSpectrumAxes(QPainter &p, int card, const QRectF &plot, bool stereo);
    ScopeCardInspector *cardInspector_;
    QPushButton *addCard_, *removeCard_;
    struct SpectrumState {
        quint64 generation = 0, sequence = 0;
        int position = -1;
        bool stereo = false;
        QVector<float> left, right;
    };
    std::array<SpectrumState, VoiceMask::Capacity+1> spectrumStates_{};
    struct PitchState {
        quint64 generation = 0, sequence = 0;
        int position = -1;
        float hz = -1;
    };
    std::array<PitchState, VoiceMask::Capacity+1> pitchStates_{};
    void editEffects();
    void restoreEffects();
    void rememberEffects();
    void editWaves();
    void restoreWaves();
    void rememberWaves();
    int frameRate_ = ScopePacing::DefaultFps;
    QChronoTimer *presentationTimer_;
    WaveOptions waveOptions_;
    struct WaveState {
        ScopeWave wave;
        quint64 generation = 0, sequence = 0;
        int position = -1;
        bool stereo = false;
    };
    std::array<WaveState, VoiceMask::Capacity+1> waveStates_{};
    ScopeGpu *gpu_ = nullptr;
    bool gpuFailed_ = false;
    QImage chromeImage_;
    QByteArray chromeKey_;
    ScopeEffects effects_;
    void updateVisibility();
    void updateEditorGeometry();
    void applyWindowSize();
    void updateWindowSizeControls();
    void layoutChanged();
    void resetChannels(bool resetGrid);
    bool hideEmptyChannels();
    void resetGrid();
    void rebuildList();
    void updateList();
    ScopeCaptureRequest captureRequest() const;
    QString channelName(int channel) const;
    bool channelReady(int channel) const;
    bool channelStereo(int channel) const;
    int slotAt(const QPointF &point) const;
    const ScopeGrid &displayLayout() const;
    QVector<int> displayedChannels() const;
    QRectF cellsRect(const QRect &cells) const;
    QRectF colorButtonRect(const QRectF &card) const;
    int panelAt(const QPointF &point) const;
    int resizeEdgesAt(const QRectF &card, const QPointF &point) const;
    QVector<int> dropOrder() const;
    void previewGesture(const QPointF &point);
    QRectF gestureTarget() const;
    QHash<int, QRectF> panelPositions() const;
    void animateLayout(const QHash<int, QRectF> &from);
    void cancelDrag();
    void editColors(int channel = -1);
    void editWindowBackground();
    void refreshThemes();
    void restoreTheme();
    void rememberTheme(bool reportFailure = true);
    void markThemeEdited();
    void applyTheme();
    void saveTheme();
    void setupPresets(QVBoxLayout *layout);
    ScopePreset currentPreset() const;
    void applyPreset(const ScopePreset &preset, const QString &file);
    bool restoreDefaultPreset();
    void refreshPresets();
    void updatePresetStatus();
    void presetEdited();
    void savePreset();
    QComboBox *presets_ = nullptr;
    QLabel *presetStatus_ = nullptr;
    QPushButton *presetDefault_ = nullptr, *presetSave_ = nullptr;
    QString presetFile_, defaultPresetFile_, suggestedPresetName_;
    QByteArray savedPreset_;
    bool applyingPreset_ = false, presetModified_ = false;
    TitleBar *titleBar_;
    QPushButton *edit_;
    QPushButton *windowSize_ = nullptr;
    QWidget *windowSizeControls_;
    QSpinBox *pixelWidth_, *pixelHeight_;
    QLabel *sizeRounding_;
    bool applyingWindowSize_ = false;
    QWidget *editor_, *channelPanel_;
    QTabWidget *editorTabs_;
    QWidget *gridControls_, *labelControls_, *editorActions_, *windowTools_;
    QGridLayout *settings_;
    int editorRows_ = 0;
    QSpinBox *rows_, *columns_;
    QCheckBox *keepGrid_, *numbers_, *names_, *stereo_;
    QListWidget *channelList_;
    QLabel *summary_;
    ScopeGrid gridLayout_, previewLayout_;
    ScopeTheme theme_;
    QComboBox *themes_;
    QString themePath_;
    QDialog *colorDialog_ = nullptr;
    quint64 colorRevision_ = 0;
    bool newFile_ = true, dragging_ = false;
    int pressedChannel_ = -1, dropSlot_ = -1;
    int pressedSolo_ = -1;
    bool soloMoved_ = false;
    int pressedColor_ = -1, hoveredColor_ = -1;
    int resizeChannel_ = -1, resizeEdges_ = 0;
    QSize resizeSpan_, originalSpan_;
    bool previewValid_ = false;
    QPointF dragStart_, dragPosition_;
    QRectF dragCard_;
    QTimer *motion_;
    QElapsedTimer motionClock_;
    QHash<int, QRectF> animationFrom_, animationTo_;
    PlayerState state_;
    ScopeFrame frame_;
    OutputFrame output_;
    QVector<float> outputMono_;
    bool outputCapturing_ = false;
};

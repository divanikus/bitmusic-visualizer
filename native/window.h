#pragma once
#include "engine.h"
#include "chrome.h"
#include "scopetheme.h"
#include "scopegpu.h"
#include "scopewave.h"
#include "scopegrid.h"
#include <QWidget>
#include <QSlider>
#include <QHash>
#include <QElapsedTimer>
#include <QColor>
class QLabel;
class QPushButton;
class QListWidget;
class QTabWidget;
class QToolButton;
class QGridLayout;
class QCloseEvent;
class QCheckBox;
class QSpinBox;
class QTimer;
class QChronoTimer;
class QDialog;
class QComboBox;

class Timeline : public QSlider {
public:
    explicit Timeline(QWidget *parent = nullptr) : QSlider(Qt::Horizontal, parent) {}
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    int valueAt(qreal x) const;
};
class ScopeWindow : public FrameWindow {
public:
    ScopeWindow();
    void present(const PlayerState &state, const ScopeFrame &frame, const OutputFrame &output = {});
    void beginFile();
    std::function<void(bool)> onVisibilityChanged;
    std::function<void(uint32_t)> onChannelsChanged;
    std::function<void(bool)> onOutputChanged;
    std::function<void()> onRefreshRequested;
    std::function<void(int)> onFrameRateChanged;
    int frameRate() const { return frameRate_; }
    bool outputEnabled() const;
    bool renderingVisible() const { return isVisible() && !isMinimized(); }
    QVector<int> visibleChannels() const;
    QRectF cardRect(int slot) const;
    QRectF panelRect(int channel) const;
protected:
    void paintEvent(QPaintEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void changeEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    void paintContents(QPainter &p, bool waves);
    QVector<ScopeLane> waveLanes();
    QByteArray chromeKey(bool dynamic = true) const;
    enum class ViewKind { Waveform, Spectrum, Keyboard };
    struct Card { int source = 0; ViewKind kind = ViewKind::Waveform; };
    QVector<Card> cards_;
    int sourceFor(int card) const { return card >= 0 && card < cards_.size() ? cards_[card].source : card; }
    ViewKind viewFor(int card) const { return card >= 0 && card < cards_.size() ? cards_[card].kind : ViewKind::Waveform; }
    QString sourceName(int source) const;
    QString viewName(ViewKind kind) const;
    void updateViewEditor();
    void addCard();
    void paintKeyboard(QPainter &p, int card, const QRectF &plot, bool muted);
    void paintSpectrumAxes(QPainter &p, int card, const QRectF &plot, bool stereo);
    QComboBox *cardView_;
    QPushButton *addCard_, *removeCard_;
    struct SpectrumState {
        quint64 generation = 0, sequence = 0;
        int position = -1;
        bool stereo = false;
        QVector<float> left, right;
    };
    std::array<SpectrumState, 33> spectrumStates_{};
    void editEffects();
    void editRenderer();
    void restoreEffects();
    void rememberEffects();
    void editWaves();
    void restoreWaves();
    void rememberWaves();
    void setFrameRate(int fps);
    int frameRate_ = ScopePacing::DefaultFps;
    QChronoTimer *presentationTimer_;
    WaveOptions waveOptions_;
    struct WaveState {
        ScopeWave wave;
        quint64 generation = 0, sequence = 0;
        int position = -1;
        bool stereo = false;
    };
    std::array<WaveState, 33> waveStates_{};
    ScopeGpu *gpu_ = nullptr;
    bool gpuFailed_ = false;
    QImage chromeImage_;
    QByteArray chromeKey_;
    ScopeEffects effects_;
    void updateVisibility();
    void updateEditorGeometry();
    void layoutChanged();
    void resetChannels(bool resetGrid);
    void resetGrid();
    void rebuildList();
    void updateList();
    uint32_t visibleMask() const;
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
    TitleBar *titleBar_;
    QPushButton *edit_;
    QWidget *editor_, *channelPanel_;
    QWidget *gridControls_, *labelControls_, *editorActions_;
    QGridLayout *settings_;
    int editorRows_ = 0;
    QSpinBox *rows_, *columns_;
    QCheckBox *keepGrid_, *numbers_, *names_, *stereo_;
    QListWidget *channelList_;
    QLabel *summary_;
    QVector<int> order_;
    QVector<bool> hidden_;
    QVector<QSize> spans_;
    ScopeGrid gridLayout_, previewLayout_;
    ScopeTheme theme_;
    QComboBox *themes_;
    QString themePath_;
    QDialog *colorDialog_ = nullptr;
    quint64 colorRevision_ = 0;
    bool newFile_ = true, dragging_ = false;
    int pressedChannel_ = -1, dropSlot_ = -1;
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

class PlayerWindow : public FrameWindow {
public:
    PlayerWindow();
    ~PlayerWindow() override;
    void loadFile(const QString &path, int song = 0);
    NativePlayer &player() { return player_; }
    ScopeWindow &scopeWindow() { return scopes_; }
    void refresh();
protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
private:
    void refreshControls();
    void refreshScopes();
    void rebuildChannels(const PlayerState &state);
    void setScopesVisible(bool visible);
    void playSong(int song);
    NativePlayer player_;
    ScopeWindow scopes_;
    QLabel *title_, *subtitle_, *clock_, *total_, *status_;
    QPushButton *play_, *stop_, *previous_, *next_, *repeat_, *showScopes_;
    QToolButton *leftOutput_, *rightOutput_;
    Timeline *timeline_;
    QListWidget *songs_;
    QTabWidget *tabs_;
    QGridLayout *channels_;
    QString shownPath_;
    int shownSong_ = -1;
    bool openedOnce_ = false;
    int unknownRangeMs_ = 180000;
};

QString timeText(int milliseconds);

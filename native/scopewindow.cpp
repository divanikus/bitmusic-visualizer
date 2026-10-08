#include "window.h"
#include "palette.h"
#include "appsettings.h"
#include "renderersettings.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDataStream>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QMessageBox>
#include <QSettings>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QTimer>
#include <QChronoTimer>
#include <algorithm>
#include <cmath>

namespace {
class ThemeComboBox : public QComboBox {
public:
    std::function<void()> refresh;
    void showPopup() override { if (refresh) refresh(); QComboBox::showPopup(); }
};
void drawPipette(QPainter &p, const QRectF &button, const QColor &ink, bool active) {
    p.save();
    auto fill = ink; fill.setAlpha(active ? 45 : 16);
    p.setPen(Qt::NoPen); p.setBrush(fill); p.drawRoundedRect(button, 5, 5);
    p.translate(button.center() - QPointF(9, 9));
    p.setBrush(Qt::NoBrush); p.setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolygon(QPolygonF{{3, 12}, {2, 16}, {6, 15}, {13, 8}, {10, 5}});
    p.drawLine(8, 3, 15, 10);
    p.setPen(QPen(ink, 4, Qt::SolidLine, Qt::RoundCap)); p.drawLine(12, 5, 15, 2);
    p.restore();
}
class WindowColorButton : public QPushButton {
public:
    WindowColorButton() {
        setObjectName("scopeWindowColor"); setFixedSize(30, 28); setCursor(Qt::PointingHandCursor);
        setToolTip("Window background color"); setAccessibleName(toolTip());
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        drawPipette(p, rect(), QColor("#b8cccf"), underMouse() || hasFocus() || isDown());
    }
};
class EditLayoutButton : public QPushButton {
public:
    EditLayoutButton() { setCheckable(true); setFixedSize(30, 26); setCursor(Qt::PointingHandCursor); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen); p.setBrush(QColor(isChecked() ? "#a2e5d5" : underMouse() || hasFocus() ? "#30434a" : "#202a30"));
        p.drawRoundedRect(rect(), 6, 6);
        p.translate(5, 3); p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(isChecked() ? "#133c38" : "#b8cccf"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (isChecked()) p.drawPolyline(QPolygonF{{3, 10}, {8, 15}, {17, 5}});
        else {
            p.drawPolygon(QPolygonF{{3, 17}, {4, 12}, {14, 2}, {18, 6}, {8, 16}});
            p.drawLine(12, 4, 16, 8); p.drawLine(4, 12, 8, 16);
        }
    }
};
}

ScopeWindow::ScopeWindow() {
    setWindowTitle("Bit Music - Scopes"); setAttribute(Qt::WA_QuitOnClose, false);
    setMouseTracking(true);
    restoreEffects();
    restoreWaves();
    presentationTimer_ = new QChronoTimer(ScopePacing::interval(frameRate_), this);
    presentationTimer_->setTimerType(Qt::PreciseTimer);
    connect(presentationTimer_, &QChronoTimer::timeout, this, [this] {
        if (onRefreshRequested) onRefreshRequested();
        else update();
    });
    setStyleSheet(R"(
      QWidget { color: #dce5e8; font-family: 'Segoe UI'; font-size: 9pt; }
      QWidget#scopeEditor, QWidget#scopeChannels { background: #1b252b; border-radius: 8px; }
      QPushButton { background: #293b41; border: none; border-radius: 6px; padding: 5px 10px; }
      QPushButton:hover { background: #385158; }
      QPushButton:checked { background: #a2e5d5; color: #133c38; }
      QSpinBox { background: #293b41; border: 1px solid #465b62; border-radius: 4px; padding: 3px; min-width: 42px; }
      QComboBox { background: #202e35; border: 1px solid #465b62; border-radius: 4px; padding: 5px; }
      QCheckBox { spacing: 5px; }
      QListWidget { background: transparent; border: none; outline: none; }
      QListWidget::item { padding: 7px 3px; }
      QListWidget::item:selected { background: #304b4b; color: #dce5e8; }
      QScrollBar:vertical { background: #1b252b; width: 8px; }
      QScrollBar::handle:vertical { background: #46575e; min-height: 24px; border-radius: 4px; }
      QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
      QLabel#editorHint { color: #92a6b0; font-size: 8pt; }
      QToolTip { color: #ecf2f3; background: #304048; border: none; padding: 5px; }
    )");
    titleBar_ = new TitleBar(this, false);
    auto close = titleBar_->findChild<QPushButton *>("windowClose");
    close->setToolTip("Hide scopes"); close->setAccessibleName("Hide scopes");
    edit_ = new EditLayoutButton; edit_->setObjectName("editLayout");
    edit_->setToolTip("Edit layout"); edit_->setAccessibleName("Edit layout"); titleBar_->addAction(edit_);
    editor_ = new QWidget(this); editor_->setObjectName("scopeEditor");
    settings_ = new QGridLayout(editor_); settings_->setContentsMargins(10, 6, 10, 6); settings_->setSpacing(10);
    gridControls_ = new QWidget; labelControls_ = new QWidget; editorActions_ = new QWidget;
    auto grid = new QHBoxLayout(gridControls_); grid->setContentsMargins(0, 0, 0, 0); grid->setSpacing(8);
    rows_ = new QSpinBox; rows_->setObjectName("gridRows"); rows_->setRange(1, 16); rows_->setValue(5); rows_->setAccessibleName("Rows");
    columns_ = new QSpinBox; columns_->setObjectName("gridColumns"); columns_->setRange(1, 8); columns_->setValue(1); columns_->setAccessibleName("Columns");
    grid->addWidget(new QLabel("Rows")); grid->addWidget(rows_);
    grid->addWidget(new QLabel("Columns")); grid->addWidget(columns_);
    keepGrid_ = new QCheckBox("Keep grid"); keepGrid_->setObjectName("keepGrid");
    keepGrid_->setToolTip("Keep rows and columns when opening another file. Card sizes, order and voice visibility reset.");
    auto actions = new QHBoxLayout(editorActions_); actions->setContentsMargins(0, 0, 0, 0); actions->setSpacing(6);
    auto background = new WindowColorButton; actions->addWidget(background);
    connect(background, &QPushButton::clicked, this, [this] { editWindowBackground(); });
    auto reset = new QPushButton("Reset"); reset->setObjectName("resetLayout"); reset->setToolTip("Restore the grid, channel order, visibility and labels. Keep the current theme."); actions->addWidget(reset);
    auto apply = new QPushButton("Apply"); apply->setObjectName("applyLayout"); apply->setToolTip("Finish editing this layout"); actions->addWidget(apply);
    apply->setStyleSheet("QPushButton { background: #a2e5d5; color: #133c38; } QPushButton:hover { background: #baf0e3; }");
    auto labels = new QHBoxLayout(labelControls_); labels->setContentsMargins(0, 0, 0, 0); labels->setSpacing(14);
    numbers_ = new QCheckBox("Numbers"); numbers_->setObjectName("channelNumbers"); numbers_->setChecked(true);
    names_ = new QCheckBox("Names"); names_->setObjectName("channelNames"); names_->setChecked(true);
    stereo_ = new QCheckBox("Stereo"); stereo_->setObjectName("scopeStereo");
    stereo_->setToolTip("Show left and right lanes with the same time and amplitude scale");
    labels->addWidget(numbers_); labels->addWidget(names_); labels->addWidget(stereo_); labels->addWidget(keepGrid_);
    settings_->addWidget(gridControls_, 0, 0); settings_->addWidget(labelControls_, 0, 1); settings_->addWidget(editorActions_, 0, 2);
    summary_ = new QLabel; summary_->setObjectName("editorHint");
    channelPanel_ = new QWidget(this); channelPanel_->setObjectName("scopeChannels");
    auto channels = new QVBoxLayout(channelPanel_); channels->setContentsMargins(10, 10, 10, 10); channels->setSpacing(4);
    channels->addWidget(new QLabel("Channel visibility"));
    auto hint = new QLabel("Drag headers to reorder.\nDrag right/bottom edges to resize."); hint->setWordWrap(true); hint->setObjectName("editorHint"); channels->addWidget(hint);
    channels->addWidget(summary_);
    channelList_ = new QListWidget; channelList_->setObjectName("scopeChannelList");
    channelList_->setDragDropMode(QAbstractItemView::InternalMove); channelList_->setDefaultDropAction(Qt::MoveAction);
    channelList_->setSelectionMode(QAbstractItemView::SingleSelection); channelList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    channels->addWidget(channelList_, 1);
    auto colors = new QPushButton("Colors..."); colors->setObjectName("scopeColors"); colors->setToolTip("Edit the selected channel's colors"); channels->addWidget(colors);
    auto visualActions = new QHBoxLayout; visualActions->setSpacing(4); channels->addLayout(visualActions);
    auto waves = new QPushButton("Waves..."); waves->setObjectName("scopeWaves"); visualActions->addWidget(waves);
    connect(waves, &QPushButton::clicked, this, [this] { editWaves(); });
    auto effects = new QPushButton("Effects..."); effects->setObjectName("scopeEffects"); visualActions->addWidget(effects);
    connect(effects, &QPushButton::clicked, this, [this] { editEffects(); });
    auto renderer = new QPushButton("Renderer..."); renderer->setObjectName("scopeRenderer"); channels->addWidget(renderer);
    renderer->setToolTip("Choose the renderer for the next application start");
    connect(renderer, &QPushButton::clicked, this, [this] { editRenderer(); });
    auto showAll = new QPushButton("Show all"); showAll->setObjectName("showAllScopes"); channels->addWidget(showAll);
    channels->addWidget(new QLabel("Theme"));
    auto themeCombo = new ThemeComboBox; themes_ = themeCombo;
    themes_->setObjectName("scopeTheme"); themes_->setAccessibleName("Theme");
    themes_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    themes_->setMinimumContentsLength(10); styleCombo(themes_);
    channels->addWidget(themes_); restoreTheme(); refreshThemes();
    themeCombo->refresh = [this] { refreshThemes(); };
    connect(themes_, &QComboBox::activated, this, [this] { applyTheme(); });
    auto save = new QPushButton("Save theme..."); save->setObjectName("saveTheme"); channels->addWidget(save);
    connect(save, &QPushButton::clicked, this, [this] { saveTheme(); });
    editor_->hide(); channelPanel_->hide();
    motion_ = new QTimer(this); motion_->setInterval(16);
    connect(motion_, &QTimer::timeout, this, [this] {
        if (motionClock_.elapsed() >= 160) { motion_->stop(); animationFrom_.clear(); animationTo_.clear(); }
        update();
    });
    connect(edit_, &QPushButton::toggled, this, [this](bool editing) {
        edit_->setToolTip(editing ? "Done" : "Edit layout"); edit_->setAccessibleName(edit_->toolTip());
        cancelDrag();
        editor_->setVisible(editing); channelPanel_->setVisible(editing); updateEditorGeometry(); update();
    });
    connect(rows_, &QSpinBox::valueChanged, this, [this] { layoutChanged(); });
    connect(columns_, &QSpinBox::valueChanged, this, [this] { layoutChanged(); });
    connect(numbers_, &QCheckBox::toggled, this, [this] { update(); });
    connect(names_, &QCheckBox::toggled, this, [this] { update(); });
    connect(stereo_, &QCheckBox::toggled, this, [this] { waveStates_ = {}; update(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        if (waveOptions_.output) { waveOptions_.output = false; rememberWaves(); }
        numbers_->setChecked(true); names_->setChecked(true); stereo_->setChecked(false); resetChannels(true);
        update();
    });
    connect(apply, &QPushButton::clicked, this, [this] { edit_->setChecked(false); });
    connect(colors, &QPushButton::clicked, this, [this] { editColors(); });
    connect(showAll, &QPushButton::clicked, this, [this] { hidden_.fill(false); layoutChanged(); });
    connect(channelList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const int channel = item->data(Qt::UserRole).toInt();
        if (channel >= 0 && channel < hidden_.size()) { hidden_[channel] = item->checkState() != Qt::Checked; layoutChanged(); }
    });
    connect(channelList_->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        order_.clear();
        for (int i = 0; i < channelList_->count(); ++i) order_.push_back(channelList_->item(i)->data(Qt::UserRole).toInt());
        layoutChanged();
    });
    if (qEnvironmentVariableIntValue("BITMUSIC_SOFTWARE_SCOPES") != 1 && qEnvironmentVariable("QT_QUICK_BACKEND") != "software") {
        gpu_ = new ScopeGpu(this, true); gpu_->lower();
        gpu_->failed = [this, effects](const QString &reason) {
            gpuFailed_ = true;
            auto failedGpu = gpu_; gpu_ = nullptr;
            if (failedGpu) { failedGpu->failed = {}; failedGpu->suspend(); failedGpu->hide(); failedGpu->deleteLater(); }
            chromeImage_ = {}; chromeKey_.clear();
            effects->setEnabled(false); effects->setToolTip("GPU effects unavailable: " + reason); update();
        };
    } else {
        effects->setEnabled(false); effects->setToolTip("Software scopes selected");
    }
    resize(1120, 700); setMinimumSize(480, 360);
    QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
    // Qt corrects off-screen placement and rejects incompatible screen scaling.
    restoreGeometry(settings.value("Windows/Scopes").toByteArray());
}

void ScopeWindow::beginFile() {
    if (gpu_) gpu_->suspend();
    newFile_ = true; frame_ = {}; output_ = {}; waveStates_ = {}; cancelDrag();
    if (onChannelsChanged) onChannelsChanged(0);
    outputCapturing_ = false;
    if (onOutputChanged) onOutputChanged(false);
}
void ScopeWindow::present(const PlayerState &state, const ScopeFrame &frame, const OutputFrame &output) {
    // A newer transport command may mark state busy after a file has already loaded.
    // Always align channel indices with new metadata, even in that intermediate state.
    const bool reset = state.valid && ((!state.busy && newFile_) || state.info.path != state_.info.path || state.info.voices != state_.info.voices);
    state_ = state;
    if (reset) { newFile_ = false; resetChannels(!keepGrid_->isChecked()); }
    frame_ = frame;
    // A bounded snapshot retry can miss an active audio callback. Keep the last
    // confirmed frame only within this transport generation, never over a seek.
    if (outputEnabled() && output.sequence && output.left.size() == ScopeFrames && output.right.size() == ScopeFrames) {
        if (output_.sequence != output.sequence || output_.generation != output.generation) {
            outputMono_.resize(ScopeFrames);
            for (int i = 0; i < ScopeFrames; ++i) outputMono_[i] = (output.left[i] + output.right[i]) * .5f;
        }
        output_ = output;
    } else if (output_.generation != state.generation || state.busy || !outputEnabled()) { output_ = {}; outputMono_.clear(); }
    titleBar_->setTitle(state.valid ? state.info.title : QString());
    stereo_->setEnabled(state.valid && state.info.stereoOutput);
    stereo_->setToolTip(state.valid && !state.info.stereoOutput ? "Mono source: one waveform per card; stereo preference is kept"
        : "Show left and right lanes with the same time and amplitude scale");
    if (renderingVisible()) update();
}
void ScopeWindow::resetGrid() {
    QSignalBlocker blockRows(rows_), blockColumns(columns_);
    const int count = int(state_.info.voices.size()) + (waveOptions_.output ? 1 : 0);
    const int columns = count > 32 ? 3 : count > 5 ? 2 : 1;
    columns_->setValue(columns); rows_->setValue(std::max(1, (count + columns - 1) / columns));
}
void ScopeWindow::resetChannels(bool grid) {
    if (colorDialog_) colorDialog_->reject();
    order_.clear(); hidden_ = QVector<bool>(ScopeTheme::FullMix + 1, false);
    spans_ = QVector<QSize>(ScopeTheme::FullMix + 1, QSize(1, 1));
    for (int i = 0; i < state_.info.voices.size(); ++i) order_.push_back(i);
    order_.push_back(ScopeTheme::FullMix); hidden_[ScopeTheme::FullMix] = !waveOptions_.output;
    cancelDrag();
    if (grid) resetGrid();
    rebuildList(); layoutChanged();
}
QVector<int> ScopeWindow::visibleChannels() const {
    QVector<int> result;
    if (!state_.valid) return result;
    for (int channel : order_) if (gridLayout_.contains(channel)) result.push_back(channel);
    return result;
}
uint32_t ScopeWindow::visibleMask() const {
    uint32_t mask = 0;
    for (int channel : visibleChannels()) if (channel != ScopeTheme::FullMix) mask |= uint32_t(1) << channel;
    return mask;
}
QString ScopeWindow::channelName(int channel) const {
    return channel == ScopeTheme::FullMix ? QString("Full mix") : state_.info.voices.value(channel);
}
bool ScopeWindow::channelReady(int channel) const {
    if (state_.busy) return false;
    if (channel == ScopeTheme::FullMix) return output_.sequence && output_.generation == state_.generation && outputMono_.size() == ScopeFrames;
    return frame_.generation == state_.generation && (frame_.mask & (uint32_t(1) << channel)) &&
        channel < frame_.channels.size() && frame_.channels[channel].size() == ScopeFrames;
}
bool ScopeWindow::channelStereo(int channel) const {
    if (!state_.info.stereoOutput || !stereo_->isChecked()) return false;
    if (channel == ScopeTheme::FullMix) return true;
    return channel < frame_.left.size() && channel < frame_.right.size() &&
        frame_.left[channel].size() == ScopeFrames && frame_.right[channel].size() == ScopeFrames;
}
void ScopeWindow::layoutChanged() {
    cancelDrag();
    for (auto &span : spans_) span = QSize(std::clamp(span.width(), 1, columns_->value()), std::clamp(span.height(), 1, rows_->value()));
    gridLayout_ = state_.valid ? packScopes(rows_->value(), columns_->value(), order_, hidden_, spans_) : ScopeGrid();
    if (hidden_.size() > ScopeTheme::FullMix && waveOptions_.output == hidden_[ScopeTheme::FullMix]) {
        waveOptions_.output = !hidden_[ScopeTheme::FullMix]; rememberWaves();
    }
    updateList();
    if (onChannelsChanged) onChannelsChanged(newFile_ ? 0 : visibleMask());
    const bool capture = outputEnabled();
    if (capture != outputCapturing_) {
        outputCapturing_ = capture; output_ = {}; outputMono_.clear(); waveStates_[ScopeTheme::FullMix] = {};
        if (onOutputChanged) onOutputChanged(capture);
    }
    update();
}
void ScopeWindow::rebuildList() {
    const int selected = channelList_->currentItem() ? channelList_->currentItem()->data(Qt::UserRole).toInt() : 0;
    QSignalBlocker block(channelList_); channelList_->clear();
    for (int channel : order_) {
        auto item = new QListWidgetItem(channelList_); item->setData(Qt::UserRole, channel);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        item->setCheckState(hidden_[channel] ? Qt::Unchecked : Qt::Checked);
        if (channel == selected) channelList_->setCurrentItem(item);
    }
}
void ScopeWindow::updateList() {
    const bool preview = dragging_ || resizeChannel_ >= 0;
    const auto visible = preview ? displayedChannels() : visibleChannels();
    QSignalBlocker block(channelList_);
    int enabled = 0;
    for (int channel : order_) if (!hidden_[channel]) ++enabled;
    for (int i = 0; i < channelList_->count(); ++i) {
        auto item = channelList_->item(i); const int channel = item->data(Qt::UserRole).toInt();
        const auto label = channel == ScopeTheme::FullMix ? channelName(channel) : QString("%1  %2").arg(channel + 1, 2, 10, QLatin1Char('0')).arg(channelName(channel));
        const auto status = hidden_[channel] ? "Hidden" : visible.contains(channel) ? "Visible" : "Overflow";
        const auto span = preview && previewValid_ && channel == resizeChannel_ ? resizeSpan_ : spans_[channel];
        const auto size = QString("%1 x %2").arg(span.width()).arg(span.height());
        item->setText(label + (span == QSize(1, 1) ? QString() : "  [" + size + "]"));
        item->setToolTip(label + " - " + status + "\nSize: " + size + " cells (columns x rows).\nMove earlier, hide another card or enlarge the grid if it overflows.");
        if (channel == ScopeTheme::FullMix) item->setToolTip(item->toolTip() + "\nFinal audio after volume, voice mutes and effects.");
        item->setForeground(QColor(visible.contains(channel) ? "#c3f1e7" : "#81969f"));
        item->setCheckState(hidden_[channel] ? Qt::Unchecked : Qt::Checked);
    }
    summary_->setText((preview ? "Preview: " : "") + QString("%1 shown / %2 overflow").arg(visible.size()).arg(enabled - visible.size()));
}
void ScopeWindow::updateVisibility() {
    if (renderingVisible()) presentationTimer_->start(); else presentationTimer_->stop();
    if (gpu_) { gpu_->suspend(); gpu_->setVisible(renderingVisible() && !gpuFailed_); }
    frame_ = {}; output_ = {}; waveStates_ = {}; cancelDrag();
    if (onVisibilityChanged) onVisibilityChanged(renderingVisible());
    update();
}
void ScopeWindow::setFrameRate(int fps) {
    fps = ScopePacing::normalize(fps);
    if (frameRate_ == fps) return;
    frameRate_ = fps;
    presentationTimer_->setInterval(ScopePacing::interval(fps));
    if (onFrameRateChanged) onFrameRateChanged(fps);
}
void ScopeWindow::showEvent(QShowEvent *event) { FrameWindow::showEvent(event); updateVisibility(); }
void ScopeWindow::hideEvent(QHideEvent *event) { FrameWindow::hideEvent(event); updateVisibility(); }
void ScopeWindow::changeEvent(QEvent *event) {
    FrameWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) updateVisibility();
}
void ScopeWindow::updateEditorGeometry() {
    titleBar_->setGeometry(14, 6, width() - 28, 30);
    const int inlineWidth = gridControls_->sizeHint().width() + labelControls_->sizeHint().width() + editorActions_->sizeHint().width() + 40;
    const int lines = width() - 28 >= inlineWidth ? 1 : 2;
    if (editorRows_ != lines) {
        editorRows_ = lines;
        for (auto widget : {gridControls_, labelControls_, editorActions_}) settings_->removeWidget(widget);
        settings_->setColumnStretch(1, lines == 1 ? 1 : 0);
        settings_->setColumnStretch(0, lines == 2 ? 1 : 0);
        settings_->addWidget(gridControls_, 0, 0, Qt::AlignLeft);
        settings_->addWidget(labelControls_, lines == 1 ? 0 : 1, lines == 1 ? 1 : 0, 1, lines == 1 ? 1 : 2, Qt::AlignLeft);
        settings_->addWidget(editorActions_, 0, lines == 1 ? 2 : 1, Qt::AlignRight);
    }
    editor_->setGeometry(14, 44, width() - 28, lines == 1 ? 42 : 80);
    const int top = editor_->geometry().bottom() + 13;
    channelPanel_->setGeometry(width() - 214, top, 200, std::max(80, height() - top - 14));
}
void ScopeWindow::resizeEvent(QResizeEvent *event) {
    FrameWindow::resizeEvent(event); cancelDrag(); updateEditorGeometry();
    if (gpu_ && gpu_->checkHealth()) gpu_->setGeometry(rect());
}
void ScopeWindow::closeEvent(QCloseEvent *event) { hide(); event->ignore(); }
QByteArray ScopeWindow::chromeKey() const {
    QByteArray key; QDataStream s(&key, QIODevice::WriteOnly);
    s << size() << devicePixelRatioF() << theme_.windowBackground << rows_->value() << columns_->value()
      << edit_->isChecked() << numbers_->isChecked() << names_->isChecked() << stereo_->isChecked()
      << state_.info.stereoOutput << state_.busy << (frame_.generation == state_.generation)
      << state_.muteMask << state_.outputMask << state_.scopeError << frame_.mask << hoveredColor_ << pressedColor_;
    s << channelReady(ScopeTheme::FullMix) << state_.playing;
    for (int ch : displayedChannels()) {
        const auto &c = theme_.colors(ch);
        s << ch << panelRect(ch) << channelName(ch) << c.waveform << c.background << c.label << c.axis << c.border
          << (ch < frame_.channels.size() ? frame_.channels[ch].size() : 0)
          << (ch < frame_.left.size() ? frame_.left[ch].size() : 0)
          << (ch < frame_.right.size() ? frame_.right[ch].size() : 0);
    }
    return key;
}
QVector<ScopeLane> ScopeWindow::waveLanes() {
    QVector<ScopeLane> lanes;
    if (state_.busy) return lanes;
    auto append = [&](int ch, const QRectF &plot, const QVector<float> &left, const QVector<float> &right,
                      bool stereo, int position, quint64 sequence, bool muted) {
        if (plot.width() < 2 || plot.height() < 2) return;
        auto &view = waveStates_[ch];
        if (view.generation != state_.generation || view.stereo != stereo) view = {};
        if (view.position != position || view.sequence != sequence) {
            view.wave.update(left, right, position, waveOptions_);
            view.generation = state_.generation; view.position = position; view.sequence = sequence; view.stereo = stereo;
        }
        const float scale = float(plot.height() * (stereo ? .21 : .44)) / view.wave.peak;
        const int points = std::max(2, std::min(2048, int(plot.width()*2)));
        for (int side = 0; side < (stereo ? 2 : 1); ++side) {
            ScopeLane lane; lane.id = ch*2+side; lane.clip = plot; lane.color = theme_.colors(ch).waveform;
            if (muted || !state_.outputMask || (stereo && !(state_.outputMask & (1u << side)))) lane.color.setAlpha(115);
            const auto &row = side ? right : left;
            const double middle = stereo ? plot.top()+plot.height()*(.25+.5*side) : plot.center().y();
            lane.points.reserve(points);
            for (int j = 0; j < points; ++j) {
                const float index = view.wave.start + float(j)*2047/(points-1);
                lane.points.push_back({plot.left()+plot.width()*j/(points-1), middle-ScopeWave::sample(row, index)*scale});
            }
            lanes.push_back(std::move(lane));
        }
    };
    for (int ch : displayedChannels()) {
        if (!channelReady(ch)) continue;
        const auto plot = panelRect(ch).adjusted(12, edit_->isChecked() || names_->isChecked() || numbers_->isChecked() ? 32 : 10, -12, -10);
        if (plot.width() < 2 || plot.height() < 2) continue;
        const bool stereo = channelStereo(ch);
        if (ch == ScopeTheme::FullMix) {
            append(ch, plot, stereo ? output_.left : outputMono_, stereo ? output_.right : outputMono_, stereo, output_.positionMs, output_.sequence, false);
            continue;
        }
        const auto &left = stereo ? frame_.left[ch] : frame_.channels[ch];
        const auto &right = stereo ? frame_.right[ch] : frame_.channels[ch];
        append(ch, plot, left, right, stereo, frame_.positionMs, 0, bool(state_.muteMask & (uint32_t(1) << ch)));
    }
    return lanes;
}
void ScopeWindow::restoreEffects() {
    QSettings settings(playerSettingsPath(), playerSettingsFormat());
    settings.setFallbacksEnabled(false); settings.beginGroup("Effects");
    auto number = [&](const char *key, int fallback, int low, int high) {
        bool ok = false; const int value = settings.value(key, fallback).toInt(&ok);
        return ok ? std::clamp(value, low, high) : fallback;
    };
    auto toggle = [&](const char *key, bool fallback) {
        const auto value = settings.value(key, fallback).toString().trimmed().toLower();
        if (value == "true" || value == "1") return true;
        if (value == "false" || value == "0") return false;
        return fallback;
    };
    effects_.trail = toggle("TrailEnabled", effects_.trail);
    effects_.glow = toggle("GlowEnabled", effects_.glow);
    effects_.trailMs = number("TrailDurationMs", effects_.trailMs, 60, 999);
    effects_.trailStrength = number("TrailBrightness", qRound(effects_.trailStrength*100), 0, 100)/100.f;
    effects_.glowStrength = number("GlowBrightness", qRound(effects_.glowStrength*100), 0, 100)/100.f;
}
void ScopeWindow::rememberEffects() {
    QSettings settings(playerSettingsPath(), playerSettingsFormat());
    settings.setFallbacksEnabled(false); settings.beginGroup("Effects");
    settings.setValue("TrailEnabled", effects_.trail); settings.setValue("GlowEnabled", effects_.glow);
    settings.setValue("TrailDurationMs", effects_.trailMs);
    settings.setValue("TrailBrightness", qRound(effects_.trailStrength*100));
    settings.setValue("GlowBrightness", qRound(effects_.glowStrength*100));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        QMessageBox::warning(this, "Could not save settings",
            "The effects are applied for this session, but could not be saved to:\n" + playerSettingsPath() +
            "\n\nPlease check that this settings location is writable.");
}
void ScopeWindow::editRenderer() {
    QDialog dialog(this); dialog.setWindowTitle("Scope renderer"); dialog.setObjectName("scopeRendererDialog");
    dialog.setMinimumWidth(330);
    auto layout = new QVBoxLayout(&dialog);
    auto current = new QLabel("Currently using: " + (gpu_ ? gpu_->backend() : QString("CPU (no effects)")));
    current->setObjectName("currentRenderer"); layout->addWidget(current);
    auto choices = new QComboBox; choices->setObjectName("rendererChoice"); choices->setAccessibleName("Renderer");
    styleCombo(choices); choices->addItem("Auto (recommended)", "auto");
#ifdef Q_OS_WIN
    choices->addItem("Direct3D 11", "d3d11");
#endif
    choices->addItem("OpenGL", "opengl"); choices->addItem("CPU (no effects)", "cpu");
    const auto previous = scopeRendererPreference(); choices->setCurrentIndex(choices->findData(previous));
    layout->addWidget(choices);
    auto note = new QLabel("Changes take effect after restarting the app.\nCPU rendering has no trail or glow.");
    note->setObjectName("editorHint"); layout->addWidget(note);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (choices->currentData().toString() != previous) {
            QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
            settings.setValue("Renderer/Backend", choices->currentData().toString()); settings.sync();
            if (settings.status() != QSettings::NoError) {
                QMessageBox::warning(&dialog, "Could not save settings",
                    "The renderer choice could not be saved to:\n" + playerSettingsPath() +
                    "\n\nPlease check that this settings location is writable.");
                return;
            }
        }
        dialog.accept();
    });
    dialog.exec();
}
void ScopeWindow::editEffects() {
    QDialog dialog(this); dialog.setWindowTitle("Scope effects"); dialog.setObjectName("scopeEffectsDialog");
    dialog.setMinimumWidth(310);
    auto layout = new QVBoxLayout(&dialog);
    auto renderer = new QLabel("Renderer: " + (gpu_ ? gpu_->backend() : QString("Software"))); layout->addWidget(renderer);
    auto form = new QFormLayout; layout->addLayout(form);
    auto trail = new QCheckBox("Fading trail"); trail->setObjectName("effectTrail"); trail->setChecked(effects_.trail); form->addRow(trail);
    auto duration = new QSpinBox; duration->setObjectName("effectDuration"); duration->setRange(60, 999); duration->setSingleStep(20);
    duration->setSuffix(" ms"); duration->setValue(effects_.trailMs); form->addRow("Trail duration", duration);
    auto strength = new QSpinBox; strength->setObjectName("effectTrailStrength"); strength->setRange(0, 100); strength->setSuffix(" %");
    strength->setValue(qRound(effects_.trailStrength*100)); form->addRow("Trail brightness", strength);
    auto glow = new QCheckBox("Line glow"); glow->setObjectName("effectGlow"); glow->setChecked(effects_.glow); form->addRow(glow);
    auto glowStrength = new QSpinBox; glowStrength->setObjectName("effectGlowStrength"); glowStrength->setRange(0, 100); glowStrength->setSuffix(" %");
    glowStrength->setValue(qRound(effects_.glowStrength*100)); form->addRow("Glow brightness", glowStrength);
    auto note = new QLabel("Effects apply to all cards.\nChanges are remembered automatically."); note->setObjectName("editorHint"); layout->addWidget(note);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::RestoreDefaults); layout->addWidget(buttons);
    bool edited = false;
    auto changed = [&, this] {
        edited = true;
        effects_ = {trail->isChecked(), glow->isChecked(), duration->value(), strength->value()/100.f, glowStrength->value()/100.f};
        duration->setEnabled(effects_.trail); strength->setEnabled(effects_.trail); glowStrength->setEnabled(effects_.glow); update();
    };
    connect(trail, &QCheckBox::toggled, &dialog, changed); connect(glow, &QCheckBox::toggled, &dialog, changed);
    for (auto spin : {duration, strength, glowStrength}) connect(spin, &QSpinBox::valueChanged, &dialog, changed);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] {
        const ScopeEffects defaults;
        trail->setChecked(defaults.trail); glow->setChecked(defaults.glow); duration->setValue(defaults.trailMs);
        strength->setValue(qRound(defaults.trailStrength*100)); glowStrength->setValue(qRound(defaults.glowStrength*100));
    });
    changed(); edited = false; dialog.exec();
    // The controls preview live; Close, Escape and the title-bar X all retain
    // those edits. Flush here so normal application exit needs no extra prompt.
    if (edited) rememberEffects();
}
void ScopeWindow::paintEvent(QPaintEvent *event) {
    FrameWindow::paintEvent(event);
    // During a drag the floating card can cover another card's waveform. Use the
    // original painter for this brief animation so its complete stacking stays exact.
    const bool gpu = gpu_ && !gpuFailed_ && !dragging_ && resizeChannel_ < 0 && animationFrom_.isEmpty() && renderingVisible();
    if (gpu_) {
        gpu_->setVisible(gpu);
        if (!gpu) gpu_->suspend();
    }
    if (gpu) {
        const auto key = chromeKey();
        if (key != chromeKey_ || chromeImage_.isNull()) {
            chromeKey_ = key;
            chromeImage_ = QImage(size()*devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
            chromeImage_.setDevicePixelRatio(devicePixelRatioF()); chromeImage_.fill(QColor("#171e24"));
            QPainter chrome(&chromeImage_); chrome.setRenderHint(QPainter::Antialiasing); paintContents(chrome, false);
        }
        gpu_->submit(chromeImage_, waveLanes(), key, state_.generation, frame_.positionMs, state_.playing && !state_.busy, effects_, output_.sequence);
        return;
    }
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    paintContents(p, true);
}
void ScopeWindow::paintContents(QPainter &p, bool waves) {
    const auto lanes = waves ? waveLanes() : QVector<ScopeLane>();
    p.fillRect(rect().adjusted(1, 1, -1, -1), theme_.windowBackground);
    // Keep the title and window controls legible on arbitrary canvas colors.
    p.setPen(Qt::NoPen); p.setBrush(QColor("#171e24")); p.drawRoundedRect(titleBar_->geometry(), 6, 6);
    const auto visible = displayedChannels(); const bool editing = edit_->isChecked();
    if (editing) {
        for (int slot = 0; slot < rows_->value() * columns_->value(); ++slot) {
            p.setPen(QPen(QColor("#314047"), 1, Qt::DashLine)); p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(cardRect(slot).adjusted(1, 1, -1, -1), 8, 8);
        }
    }
    auto drawPanel = [&](int channel, bool floating) {
        const auto card = panelRect(channel); p.save();
        if (floating) {
            p.setPen(Qt::NoPen); p.setBrush(QColor(0, 0, 0, 100));
            p.drawRoundedRect(card.adjusted(-3, 3, 5, 8), 10, 10);
        }
        p.setClipRect(card);
        const bool mix = channel == ScopeTheme::FullMix;
        const bool muted = (!mix && (state_.muteMask & (uint32_t(1) << channel))) || state_.outputMask == 0;
        const auto colors = theme_.colors(channel);
        auto dimmed = [muted](QColor color) { if (muted) color.setAlpha(115); return color; };
        p.setPen(QPen(floating ? QColor("#a2e5d5") : colors.border, floating ? 2 : 1));
        p.setBrush(colors.background); p.drawRoundedRect(card.adjusted(1, 1, -1, -1), 8, 8);
        p.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
        p.setPen(dimmed(colors.label));
        QStringList label;
        if (!mix && numbers_->isChecked()) label << QString("%1").arg(channel + 1, 2, 10, QLatin1Char('0'));
        if (names_->isChecked() || (mix && numbers_->isChecked())) label << channelName(channel);
        const auto textRect = card.adjusted(editing ? 28 : 12, 5, editing ? -58 : -12, 0);
        p.drawText(QRectF(textRect.left(), textRect.top(), std::max(0.0, textRect.width()), 22), Qt::AlignVCenter,
                   p.fontMetrics().elidedText(label.join("   "), Qt::ElideRight, std::max(0, int(textRect.width()))));
        if (editing) {
            const auto button = colorButtonRect(card);
            if (!button.isEmpty()) {
                const QColor ink(colors.background.lightnessF() > .55 ? "#243a43" : "#b8cccf");
                drawPipette(p, button, ink, hoveredColor_ == channel || pressedColor_ == channel);
            }
            p.setPen(Qt::NoPen); p.setBrush(QColor("#93aab0"));
            for (int x = 0; x < 2; ++x) for (int y = 0; y < 3; ++y) p.drawEllipse(QPointF(card.left() + 10 + x * 5, card.top() + 10 + y * 5), 1.1, 1.1);
            p.setPen(QPen(QColor("#93aab0"), 1.4));
            p.drawLine(QPointF(card.right() - 19, card.top() + 10), QPointF(card.right() - 11, card.top() + 18));
            p.drawLine(QPointF(card.right() - 11, card.top() + 10), QPointF(card.right() - 19, card.top() + 18));
            if (card.width() >= 30 && card.height() >= 42) {
                const QColor ink(colors.background.lightnessF() > .55 ? "#243a43" : "#b8cccf");
                p.setPen(QPen(ink, 1.5));
                for (int offset : {5, 10}) p.drawLine(card.bottomRight()-QPointF(offset+4, 4), card.bottomRight()-QPointF(4, offset+4));
            }
        }
        const auto plot = card.adjusted(12, editing || !label.isEmpty() ? 32 : 10, -12, -10);
        if (plot.width() < 2 || plot.height() < 2) { p.restore(); return; }
        const bool stereo = channelStereo(channel);
        p.setPen(colors.axis);
        for (int side = 0; side < (stereo ? 2 : 1); ++side) {
            const double middle = stereo ? plot.top() + plot.height() * (.25 + .5 * side) : plot.center().y();
            p.drawLine(QPointF(plot.left(), middle), QPointF(plot.right(), middle));
        }
        if (!channelReady(channel)) {
            p.setFont(QFont("Segoe UI", 8)); p.setPen(colors.label);
            p.drawText(plot, Qt::AlignCenter, mix ? (state_.playing || state_.busy ? "Waiting for audio..." : "No output") :
                state_.scopeError.isEmpty() ? "Preparing..." : "Scope unavailable");
            p.restore(); return;
        }
        if (stereo && plot.height() >= 40) {
            for (int side = 0; side < 2; ++side) {
                auto color = colors.label;
                if (muted || !(state_.outputMask & (1u << side))) color.setAlpha(115);
                p.setPen(color); p.setFont(QFont("Segoe UI", 8));
                p.drawText(QPointF(plot.left(), plot.top() + plot.height() * (.09 + .5 * side)), side ? "R" : "L");
            }
        }
        if (!waves) { p.restore(); return; }
        p.setClipRect(plot, Qt::IntersectClip);
        for (const auto &lane : lanes) if (lane.id/2 == channel && !lane.points.isEmpty()) {
            QPainterPath line; line.moveTo(lane.points.front());
            for (int j = 1; j < lane.points.size(); ++j) line.lineTo(lane.points[j]);
            p.setPen(QPen(lane.color, 1.6)); p.setBrush(Qt::NoBrush); p.drawPath(line);
        }
        p.restore();
    };
    for (int channel : visible) if (!dragging_ || channel != pressedChannel_) drawPanel(channel, false);
    if (editing && (dragging_ || resizeChannel_ >= 0)) {
        const auto target = gestureTarget().adjusted(2, 2, -2, -2);
        if (!target.isEmpty()) {
            p.setPen(QPen(QColor(previewValid_ ? "#a2e5d5" : "#f29595"), 2, Qt::DashLine)); p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(target, 7, 7);
            const int channel = resizeChannel_ >= 0 ? resizeChannel_ : pressedChannel_;
            const auto span = resizeChannel_ >= 0 ? resizeSpan_ : spans_[channel];
            const QString label = previewValid_ ? QString("%1 x %2").arg(span.width()).arg(span.height()) : QString("Does not fit");
            p.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
            const QRectF badge(target.left()+6, target.bottom()-28, p.fontMetrics().horizontalAdvance(label)+16, 23);
            p.fillRect(badge, QColor("#202e35")); p.drawText(badge, Qt::AlignCenter, label);
        }
    }
    if (dragging_ && visible.contains(pressedChannel_)) drawPanel(pressedChannel_, true);
}

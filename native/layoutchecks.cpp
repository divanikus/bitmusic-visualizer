#include "playerwindow.h"
#include "scopecardinspector.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <QTabWidget>
#include <QComboBox>
#include <QScrollArea>
#include <QScrollBar>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void pump(QApplication &app, int ms = 220) { QEventLoop loop(&app); QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
void until(QApplication &app, const std::function<bool()> &ready, const char *message) {
    QElapsedTimer clock; clock.start(); while (!ready() && clock.elapsed() < 5000) pump(app, 20); require(ready(), message);
}
void mouse(ScopeWindow &scope, QEvent::Type type, QPointF point) {
    QMouseEvent event(type, point, scope.mapToGlobal(point.toPoint()), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&scope, &event);
}
void checkInspector() {
    ScopeCardInspector inspector;
    int viewEdits = 0, spectrumEdits = 0, musicEdits = 0;
    ScopeCard music;
    inspector.onViewChanged = [&](ScopeView) { ++viewEdits; };
    inspector.onSpectrumChanged = [&](bool, bool) { ++spectrumEdits; };
    inspector.onMusicChanged = [&](const ScopeCard &options) { ++musicEdits; music = options; };
    ScopeCard card; card.source = ScopeCards::FullMix; card.kind = ScopeView::Pattern;
    card.firstChannel = 2; card.keyboardSize = 65; card.commands = true; card.horizontal = true;
    inspector.setCard(&card, "Full mix", {true, true}, "Notes", 8);
    inspector.setCard(nullptr, "Select a card", {}, "", 0);
    inspector.setCard(&card, "Full mix", {true, true}, "Notes", 8);
    require(!viewEdits && !spectrumEdits && !musicEdits, "Inspector selection emitted edits while restoring fields.");
    inspector.findChild<QSpinBox *>("patternFirstChannel")->setValue(5);
    require(musicEdits == 1 && music.firstChannel == 4 && music.keyboardSize == 65 && music.commands && music.horizontal,
            "Inspector edit lost other per-card music options.");
    auto view = inspector.findChild<QComboBox *>("cardView");
    view->setCurrentIndex(view->findData(int(ScopeView::Spectrum)));
    require(viewEdits == 1 && !spectrumEdits && musicEdits == 1, "View edit leaked into another option group.");
}
void checkCardCapture() {
    ScopeCards cards; cards.reset(70, true, {});
    cards[0].kind = ScopeView::Pattern; cards[1].kind = ScopeView::PianoRoll;
    const int duplicate = cards.add({0, ScopeView::Waveform}, 0);
    // Only packed cards request signals; the high voice exercises multiword masks.
    ScopeGrid displayed{{0, {}}, {1, {}}, {duplicate, {}}, {69, {}}, {ScopeCards::FullMix, {}}};
    auto request = cards.captureRequest(displayed);
    require(request.channels == (VoiceMask::bit(0) | VoiceMask::bit(69)) && request.output,
            "Capture request lost duplicate/high sources or included event-only/overflow cards.");
    cards[duplicate].kind = ScopeView::Pattern; cards[ScopeCards::FullMix].kind = ScopeView::PianoRoll;
    request = cards.captureRequest(displayed);
    require(request.channels == VoiceMask::bit(69) && !request.output, "Event-only cards requested audio without Levels.");
    cards.setAllLevels(true);
    request = cards.captureRequest(displayed);
    require(request.channels == (VoiceMask::bit(0) | VoiceMask::bit(1) | VoiceMask::bit(69)) && request.output,
            "Levels failed to request PCM for event cards or Full mix.");
    displayed.remove(ScopeCards::FullMix);
    require(!cards.captureRequest(displayed).output, "Overflow Full mix kept output capture enabled.");
    require(!cards.cycleSourceView(100, {}), "Cycling a source without cards must remain a no-op.");
    cards.cycleSourceView(0, {});
    require(cards.viewFor(0) == ScopeView::Waveform && cards.viewFor(duplicate) == ScopeView::Waveform,
            "Source cycle failed to skip unsupported views or synchronize duplicates.");
    cards.cycleSourceView(0, {true, true});
    require(cards.viewFor(0) == ScopeView::Spectrum, "Source view cycle changed its order.");
    cards.setAllViews(ScopeView::Keyboard);
    require(cards.viewFor(69) == ScopeView::Keyboard && cards.viewFor(ScopeCards::FullMix) == ScopeView::Keyboard,
            "Set all views missed a high source or Full mix.");
}
void checkAutomaticVisibility() {
    ScopeCards cards; cards.reset(3, false, VoiceMask::first(3));
    cards[1].kind = ScopeView::Spectrum; cards[1].levels = true; cards[1].span = {2, 1};
    const int extra = cards.add({1, ScopeView::Keyboard}, 1);
    cards.hideEmpty(VoiceMask::first(3));
    cards.setHidden(0, true); // A manual hide must outlive automatic usage changes.
    cards.remove(2);
    const auto order = cards.order();
    require(cards.hideEmpty({}) && cards[0].hidden && !cards[1].hidden && !cards[extra].hidden &&
            cards[ScopeCards::FullMix].hidden && cards.order() == order && !cards.order().contains(2),
            "Changing subsong retained automatic hides or restored manual/removed cards.");
    require(cards[1].kind == ScopeView::Spectrum && cards[1].levels && cards[1].span == QSize(2, 1),
            "Automatic visibility reset card settings.");
    cards.hideEmpty(VoiceMask::bit(1));
    cards.toggleSourceVisibility(1); cards.toggleSourceVisibility(1);
    require(!cards.hideEmpty({}) && cards[1].hidden && cards[extra].hidden,
            "Source visibility shortcut did not replace automatic hiding for duplicates.");
    cards.showAll();
    require(!cards.hideEmpty({}) && !cards[0].hidden && !cards[ScopeCards::FullMix].hidden,
            "Show all left stale automatic visibility state.");
}
void checkCardState() {
    ScopeCards cards;
    cards.reset(3, false, VoiceMask::bit(1));
    require(cards.order() == QVector<int>({0, 1, 2, ScopeCards::FullMix}) &&
            cards[1].hidden && cards[ScopeCards::FullMix].hidden, "Initial card visibility or source IDs changed.");
    cards[0].span = {2, 2};
    const int extra = cards.add({0, ScopeView::Spectrum, true, false}, 0);
    require(extra > ScopeCards::FullMix && cards.order()[1] == extra && cards.sourceFor(extra) == 0,
            "Duplicate card lost its independent ID, source or insertion position.");
    cards.toggleSourceVisibility(0);
    require(cards[0].hidden && cards[extra].hidden, "Source hide did not include duplicates.");
    cards.toggleSourceVisibility(0);
    require(!cards[0].hidden && !cards[extra].hidden, "Source show did not include duplicates.");
    cards.remove(0); cards.showAll();
    require(!cards.order().contains(0), "Show all restored a removed card.");
    cards.remove(extra);
    const int reused = cards.add({2, ScopeView::Keyboard}, 2);
    require(reused == extra && cards[reused].span == QSize(1, 1) && !cards[reused].spectrumBars &&
            cards[reused].spectrumGrid, "Reused card ID retained removed card options.");

    ScopePreset preset; preset.configuration.sources = {"voice:0", "voice:1", "voice:2"};
    preset.rows = 3; preset.columns = 3;
    preset.cards = {{"voice:2", 4, {2, 2}, true, true, false, true, true, 2, 60},
                    {"mix", 3, {1, 2}, false}, {"voice:2", 1, {1, 1}, false}, {"voice:0", 2}};
    for (auto &card : preset.cards) card.levels = false;
    preset.cards[0].levels = true;
    cards.loadPreset(preset, {true, true});
    ScopePreset roundtrip = preset; roundtrip.cards = cards.presetCards(preset.configuration);
    require(roundtrip.encode() == preset.encode(), "Card model changed preset fields or ordering.");
    require(cards.order()[0] == 2 && cards.order()[1] == ScopeCards::FullMix && cards.order()[2] > ScopeCards::FullMix,
            "Preset duplicates alias their original card or Full mix.");
    require(cards[2].levels && !cards[ScopeCards::FullMix].levels && cards.levelsState(false) == Qt::PartiallyChecked,
            "Per-card levels did not survive the preset or produce a mixed state.");
    const auto before = cards.pack(3, 3);
    const auto preview = cards.pack(3, 3, cards.order(), 2, {3, 3});
    require(preview.value(2).size() == QSize(3, 3) && cards.pack(3, 3) == before && cards[2].span == QSize(2, 2),
            "Packing a resize preview mutated committed card state.");
    cards.loadPreset(preset, {});
    require(cards.viewFor(2) == ScopeView::Waveform && cards.viewFor(ScopeCards::FullMix) == ScopeView::Waveform,
            "Unavailable event views did not fall back to waveforms.");
    cards.clampSpans(1, 1);
    require(cards[2].span == QSize(1, 1), "Grid resize failed to clamp card spans.");
    for (auto &card : preset.cards) card.levels.reset();
    cards.loadPreset(preset, {}, true);
    for (int id : cards.order()) require(cards[id].levels, "Legacy preset ignored the saved Levels preference.");
    cards.reset(1, true, {});
    while (cards.order().size() < ScopeCards::Maximum) require(cards.add({0}, 0) >= 0, "Card capacity shrank.");
    require(cards.add({0}, 0) == -1 && cards.order().size() == ScopeCards::Maximum, "Card capacity is not bounded.");
}
void checkPacking() {
    QVector<int> order; QVector<bool> hidden(33, false); QVector<QSize> spans(33, QSize(1, 1));
    for (int i = 0; i < 33; ++i) order << i;
    // Exercise every grid size with deterministic mixed spans and visibility.
    for (int rows = 1; rows <= 16; ++rows) for (int columns = 1; columns <= 8; ++columns) {
        for (int i = 0; i < 33; ++i) { spans[i] = QSize(1+(i*7+rows)%4, 1+(i*3+columns)%4); hidden[i] = i%7 == 6; }
        const auto layout = packScopes(rows, columns, order, hidden, spans);
        require(layout == packScopes(rows, columns, order, hidden, spans), "Grid packing is unstable.");
        QVector<int> occupied(rows*columns, -1);
        for (auto it = layout.cbegin(); it != layout.cend(); ++it) {
            const auto rect = it.value();
            require(!hidden[it.key()] && QRect(0, 0, columns, rows).contains(rect), "Span escapes grid or displays a hidden card.");
            for (int y = rect.top(); y <= rect.bottom(); ++y) for (int x = rect.left(); x <= rect.right(); ++x) {
                require(occupied[y*columns+x] < 0, "Spanning cards overlap."); occupied[y*columns+x] = it.key();
            }
        }
    }
    hidden.fill(false); spans.fill(QSize(1, 1)); spans[0] = QSize(2, 2); spans[1] = QSize(3, 2);
    auto layout = packScopes(2, 3, {0, 1, 2, 3}, hidden, spans);
    require(layout.size() == 3 && !layout.contains(1) && layout.value(2) == QRect(2, 0, 1, 1) && layout.value(3) == QRect(2, 1, 1, 1),
            "Small cards did not fill the holes around a large overflow card.");
    hidden[0] = true; layout = packScopes(2, 3, {0, 1, 2, 3}, hidden, spans);
    require(layout.size() == 1 && layout.contains(1), "Hiding a spanning card did not admit the overflow card.");
}
}
void checkLayoutSpans(QApplication &app, const QString &directory, QTextStream &log) {
    checkCardCapture(); log << "Capture policy: duplicate source union, high masks, event-only cards, Levels and overflow PASS\n";
    checkInspector(); log << "Card inspector: selection synchronization without edits, independent view/music callbacks PASS\n";
    checkAutomaticVisibility(); log << "Automatic visibility: subsong changes preserve manual hides, duplicates, removed cards and settings PASS\n";
    checkCardState(); log << "Card state: stable IDs, duplicates/removal, visibility, preset compatibility, preview isolation and capacity PASS\n";
    checkPacking(); log << "Span packing: all 128 grid sizes, bounds, no overlap, hidden/refill and hole filling PASS\n"; log.flush();
    ScopeWindow scope; scope.resize(1100, 760); scope.show();
    PlayerState state; state.valid = true; state.generation = 1; state.info.path = "span-preview"; state.info.title = "Spanning cards"; state.info.stereoOutput = true;
    ScopeFrame frame; frame.generation = 1; frame.mask = 255;
    for (int ch = 0; ch < 8; ++ch) {
        state.info.voices << QString("Voice %1").arg(ch+1);
        QVector<float> samples(ScopeFrames); for (int i = 0; i < ScopeFrames; ++i) samples[i] = 5000*std::sin((i+ch*13)*.04);
        frame.channels << samples; frame.left << samples; frame.right << samples;
    }
    OutputFrame output; output.generation = 1; output.sequence = 2; output.left = frame.channels[0]; output.right = frame.channels[1];
    scope.present(state, frame); scope.findChild<QPushButton *>("editLayout")->click();
    auto rows = scope.findChild<QSpinBox *>("gridRows"), columns = scope.findChild<QSpinBox *>("gridColumns");
    rows->setValue(3); columns->setValue(3);
    auto list = scope.findChild<QListWidget *>("scopeChannelList"); list->item(8)->setCheckState(Qt::Checked);
    scope.present(state, frame, output); pump(app);
    auto allLevels = scope.findChild<QCheckBox *>("scopeLevels"); allLevels->setChecked(false);
    list->setCurrentRow(0);
    auto cardLevels = scope.findChild<QCheckBox *>("cardLevels");
    cardLevels->setChecked(true);
    require(allLevels->checkState() == Qt::PartiallyChecked, "Individual levels did not produce a mixed toolbar state.");
    list->setCurrentRow(1); require(!cardLevels->isChecked(), "Per-card levels leaked to another source.");
    allLevels->setChecked(true); require(cardLevels->isChecked(), "All-card Levels did not update the inspector.");
    allLevels->setChecked(false);
    for (bool accept : {false, true}) {
        std::exception_ptr failure;
        QTimer::singleShot(20, &scope, [&] {
            auto dialog = scope.findChild<QDialog *>("scopeColorsDialog");
            try {
                require(dialog && dialog->windowTitle() == "Card settings", "Card settings did not open.");
                auto pages = dialog->findChild<QTabWidget *>("cardSettingsTabs");
                require(pages && pages->count() == 2 && pages->tabText(0) == "Card" && pages->tabText(1) == "Colors", "Card settings tabs missing.");
                auto type = dialog->findChild<QComboBox *>("cardView");
                type->setCurrentIndex(type->findData(int(ScopeView::Spectrum)));
                require(dialog->findChild<QComboBox *>("spectrumStyle")->isVisible(), "View options did not follow dialog type.");
                dialog->findChild<QCheckBox *>("cardLevels")->setChecked(true);
                require(allLevels->checkState() == Qt::PartiallyChecked, "Dialog Levels preview was not applied.");
                if (accept) {
                    pump(app, 30); dialog->grab().save(QDir(directory).filePath("card-settings-card.png"));
                    pages->setCurrentIndex(1); pump(app, 30); dialog->grab().save(QDir(directory).filePath("card-settings-colors.png"));
                    dialog->accept();
                } else dialog->reject();
            } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
        });
        scope.findChild<QPushButton *>("scopeColors")->click();
        if (failure) std::rethrow_exception(failure);
        require(cardLevels->isChecked() == accept && scope.findChild<QComboBox *>("cardView")->currentData().toInt() ==
                int(accept ? ScopeView::Spectrum : ScopeView::Waveform), "Dialog Save/Cancel lost view or levels state.");
    }
    scope.findChild<QComboBox *>("cardView")->setCurrentIndex(0); allLevels->setChecked(false); pump(app);
    log << "Card dialog: contextual options, per-card Levels, live preview, Save/Cancel and mixed all-card toggle PASS\n";
    auto tabs = scope.findChild<QTabWidget *>("scopeChannels");
    require(tabs && tabs->count() == 3 && tabs->tabText(0) == "Cards" && tabs->tabText(1) == "Layout" && tabs->tabText(2) == "Display",
            "Editor settings groups are missing.");
    auto inspector = scope.findChild<QScrollArea *>("cardInspector");
    require(list->verticalScrollBar()->maximum() == 0 && inspector->verticalScrollBar()->maximum() == 0,
            "Card list scrolls despite enough room for all nine cards and their settings.");
    scope.grab().save(QDir(directory).filePath("editor-1100-waveform.png"));
    auto view = scope.findChild<QComboBox *>("cardView"); list->setCurrentRow(2);
    view->setCurrentIndex(view->findData(int(ScopeWindow::ViewKind::Keyboard)));
    auto help = scope.findChild<QPushButton *>("viewHelp"); help->click(); pump(app, 40);
    require(inspector->verticalScrollBar()->maximum() == 0,
            "Expanded card help scrolls despite enough room in the editor.");
    help->click(); pump(app, 40);
    require(list->verticalScrollBar()->maximum() == 0,
            "Collapsing card help did not return free space to the list.");
    const auto selected = list->currentItem()->data(Qt::UserRole);
    const auto visible = scope.visibleChannels(); const auto beforeTabs = scope.panelRect(0);
    int tabMaskChanges = 0; scope.onChannelsChanged = [&](VoiceMask) { ++tabMaskChanges; };
    for (const QSize size : {QSize(1100,760), QSize(720,480), QSize(480,360)}) {
        scope.resize(size); pump(app, 40);
        for (int tab : {0,1,2,0}) {
            tabs->setCurrentIndex(tab); pump(app, 30);
            require(list->currentItem()->data(Qt::UserRole) == selected && scope.visibleChannels() == visible &&
                    view->currentData().toInt() == int(ScopeWindow::ViewKind::Keyboard) && !tabMaskChanges,
                    "Changing editor tabs altered card selection, views or capture.");
            require(scope.rect().contains(tabs->geometry()) && scope.panelRect(0).right() < tabs->x(),
                    "Editor overlaps the canvas or escapes a small window.");
            if (tab == 2) require(!scope.findChild<QPushButton *>("scopeRenderer"), "Renderer was not moved to application settings.");
            scope.grab().save(QDir(directory).filePath(QString("editor-%1-%2.png").arg(size.width()).arg(tabs->tabText(tab).toLower())));
        }
        inspector->ensureWidgetVisible(view); pump(app, 20);
        require(view->isVisible() && inspector->viewport()->rect().contains(QRect(view->mapTo(inspector->viewport(), QPoint()), view->size())),
                "Selected card view cannot be reached at a small size.");
    }
    scope.resize(1100,760); pump(app, 40);
    require(scope.panelRect(0) == beforeTabs, "Tab navigation changed the grid.");
    view->setCurrentIndex(view->findData(int(ScopeWindow::ViewKind::Waveform))); scope.onChannelsChanged = {};
    log << "Editor tabs: adaptive list/help, selection/view/capture retention, normal/compact/minimum bounds, reachable inspector PASS\n"; log.flush();
    VoiceMask mask = 255; int updates = 0; bool capture = scope.outputEnabled();
    scope.onChannelsChanged = [&](VoiceMask value) { mask = value; ++updates; };
    scope.onOutputChanged = [&](bool value) { capture = value; };
    auto pitch = [&] { return QPointF(scope.cardRect(1).left()-scope.cardRect(0).left(), scope.cardRect(3).top()-scope.cardRect(0).top()); };
    auto start = scope.panelRect(0).bottomRight()-QPointF(6,6), end = start+pitch();
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end); pump(app);
    require(updates == 0 && capture && scope.visibleChannels().size() == 9, "Resize preview changed capture/audio masks before release.");
    scope.grab().save(QDir(directory).filePath("native-span-resize-preview.png"));
    mouse(scope, QEvent::MouseButtonRelease, end); pump(app);
    require(mask == 63 && !capture && scope.visibleChannels().size() == 6, "Committed span did not update overflow and Full mix capture.");
    require(scope.panelRect(0) == scope.cardRect(0).united(scope.cardRect(4)), "Corner drag did not produce a 2x2 card.");
    const auto enlarged = scope.panelRect(0);
    const auto blocked = scope.panelRect(5);
    start = blocked.bottomRight()-QPointF(6,6); end = start+pitch();
    const auto visibleBefore = scope.visibleChannels(); const int beforeRejected = updates;
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end); pump(app);
    require(scope.panelRect(5) == blocked && scope.visibleChannels() == visibleBefore && updates == beforeRejected,
            "An impossible resize hid the active card or changed capture.");
    scope.grab().save(QDir(directory).filePath("native-span-rejected.png"));
    mouse(scope, QEvent::MouseButtonRelease, end); pump(app);
    start = enlarged.bottomRight()-QPointF(6,6); end = start-pitch();
    const int beforeCancel = updates;
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier); QApplication::sendEvent(&scope, &escape); pump(app);
    require(scope.panelRect(0) == enlarged && updates == beforeCancel && mask == 63, "Escape committed a resize preview.");
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, QPointF(scope.width()-10, 200));
    mouse(scope, QEvent::MouseButtonRelease, QPointF(scope.width()-10, 200)); pump(app);
    require(scope.panelRect(0) == enlarged && mask == 63, "Outside release changed a card span.");
    start = enlarged.bottomRight()-QPointF(6,6); end = start-pitch();
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end);
    scope.resize(1120, 780); pump(app);
    require(scope.panelRect(0) == scope.cardRect(0).united(scope.cardRect(4)), "Window resize committed an unfinished card gesture.");
    // Right and bottom edges act independently.
    start = QPointF(scope.panelRect(0).right()-3, scope.panelRect(0).center().y()); end = start-QPointF(pitch().x(), 0);
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end); mouse(scope, QEvent::MouseButtonRelease, end); pump(app);
    require(scope.panelRect(0) == scope.cardRect(0).united(scope.cardRect(3)), "Horizontal edge changed the wrong span dimension.");
    start = QPointF(scope.panelRect(0).center().x(), scope.panelRect(0).bottom()-3); end = start-QPointF(0, pitch().y());
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end); mouse(scope, QEvent::MouseButtonRelease, end); pump(app);
    require(scope.panelRect(0) == scope.cardRect(0) && mask == 255 && capture, "Vertical shrink did not restore overflow capture.");
    // Full mix has the same resize/reorder semantics without a hardware bit.
    require(list->model()->moveRows({}, 8, 1, {}, 0), "Cannot move Full mix first.");
    start = QPointF(scope.panelRect(ScopeTheme::FullMix).right()-3, scope.panelRect(ScopeTheme::FullMix).center().y()); end = start+QPointF(pitch().x(), 0);
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end); mouse(scope, QEvent::MouseButtonRelease, end); pump(app);
    require(scope.panelRect(ScopeTheme::FullMix) == scope.cardRect(0).united(scope.cardRect(1)) && mask == 127 && capture, "Full mix span aliases a voice or has a special size.");
    start = scope.panelRect(ScopeTheme::FullMix).topLeft()+QPointF(15,15); end = scope.cardRect(2).center();
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end); mouse(scope, QEvent::MouseButtonRelease, end); pump(app);
    require(scope.panelRect(ScopeTheme::FullMix) == scope.cardRect(1).united(scope.cardRect(2)) && list->item(1)->data(Qt::UserRole).toInt() == ScopeTheme::FullMix,
            "Dragging a wide card lost its span/order.");
    // A drop onto the second cell of the wide card targets that card, not a
    // channel at the same numeric index as the cell.
    start = scope.panelRect(0).topLeft()+QPointF(15,15);
    end = QPointF((scope.cardRect(1).right()+scope.cardRect(2).left())*.5, scope.cardRect(1).center().y());
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end); mouse(scope, QEvent::MouseButtonRelease, end); pump(app);
    require(list->item(0)->data(Qt::UserRole).toInt() == ScopeTheme::FullMix, "Drop hit testing ignored the internal cell gap of a wide card.");
    scope.present(state, frame, output); scope.findChild<QCheckBox *>("scopeStereo")->setChecked(true);
    scope.grab().save(QDir(directory).filePath("native-span-editor.png"));
    scope.findChild<QPushButton *>("applyLayout")->click(); pump(app); scope.present(state, frame, output); pump(app);
    auto picture = [&] { if (auto gpu = scope.findChild<QQuickWidget *>("scopeGpu")) return gpu->grabFramebuffer(); return scope.grab().toImage(); };
    const auto first = picture(); first.save(QDir(directory).filePath("native-span-cards.png"));
    frame.positionMs += 25; for (auto &v : frame.left[0]) v *= -1; frame.channels[0] = frame.left[0]; ++output.sequence; output.left.fill(0);
    scope.present(state, frame, output); pump(app);
    require(first != picture(), "Spanning cards stopped updating their waveform.");
    // Subsong changes retain spans; resizing the window does not mutate cells.
    ++state.generation; scope.present(state, {}); require(scope.panelRect(ScopeTheme::FullMix) == scope.cardRect(0).united(scope.cardRect(1)), "Subsong reset a card size.");
    scope.resize(640, 500); pump(app); require(scope.panelRect(ScopeTheme::FullMix) == scope.cardRect(0).united(scope.cardRect(1)), "Window resize changed the cell span.");
    scope.findChild<QPushButton *>("editLayout")->click(); columns->setValue(1);
    require(scope.panelRect(ScopeTheme::FullMix) == scope.cardRect(0), "Grid shrink did not clamp a wide card.");
    scope.findChild<QCheckBox *>("keepGrid")->setChecked(true); scope.beginFile(); scope.present(state, {});
    require(columns->value() == 1 && rows->value() == 3 && scope.panelRect(0) == scope.cardRect(0), "New file retained card order/size or lost Keep grid.");
    scope.findChild<QPushButton *>("resetLayout")->click();
    require(scope.visibleChannels().size() == 8 && !scope.outputEnabled() && scope.panelRect(0) == scope.cardRect(0), "Reset did not restore unit cards.");
    scope.onChannelsChanged = {}; scope.onOutputChanged = {}; scope.close();
    log << "Spanning UI: corner/edge resize, deferred capture masks, cancel, overflow/refill, wide-card drag/hit testing, Full mix, mono/stereo paint, file/subsong/reset PASS\n"; log.flush();

    PlayerWindow player; player.player().setVolume(0); player.show(); player.loadFile(QDir(directory).filePath("demo.vgz"));
    until(app, [&] { return player.player().state().valid && !player.player().state().busy && player.player().scopes().mask == 255; }, "Live span file did not prepare.");
    auto &live = player.scopeWindow(); live.findChild<QPushButton *>("editLayout")->click();
    live.findChild<QSpinBox *>("gridRows")->setValue(3); live.findChild<QSpinBox *>("gridColumns")->setValue(3); pump(app);
    const auto before = player.player().state();
    start = live.panelRect(0).bottomRight()-QPointF(6,6);
    end = start+QPointF(live.cardRect(1).left()-live.cardRect(0).left(), live.cardRect(3).top()-live.cardRect(0).top());
    mouse(live, QEvent::MouseButtonPress, start); mouse(live, QEvent::MouseMove, end); pump(app, 100); mouse(live, QEvent::MouseButtonRelease, end);
    until(app, [&] { const auto s = player.player().state(); auto f = player.player().scopes(); return s.positionMs > before.positionMs+200 && f.mask == 63 && f.positionMs > before.positionMs; }, "Resizing froze the remaining live VGZ scopes.");
    require(player.player().state().muteMask == before.muteMask && !player.player().state().starvations && !player.player().state().outputErrors, "Card resize changed audible voices or starved audio.");
    player.close(); log << "Live VGZ resize: advancing unchanged scopes/audio, unchanged mutes, zero audio errors PASS\n"; log.flush();
}

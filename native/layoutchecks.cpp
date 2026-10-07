#include "window.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
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
    uint32_t mask = 255; int updates = 0; bool capture = scope.outputEnabled();
    scope.onChannelsChanged = [&](uint32_t value) { mask = value; ++updates; };
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
    start = QPointF(scope.panelRect(32).right()-3, scope.panelRect(32).center().y()); end = start+QPointF(pitch().x(), 0);
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end); mouse(scope, QEvent::MouseButtonRelease, end); pump(app);
    require(scope.panelRect(32) == scope.cardRect(0).united(scope.cardRect(1)) && mask == 127 && capture, "Full mix span aliases a voice or has a special size.");
    start = scope.panelRect(32).topLeft()+QPointF(15,15); end = scope.cardRect(2).center();
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end); mouse(scope, QEvent::MouseButtonRelease, end); pump(app);
    require(scope.panelRect(32) == scope.cardRect(1).united(scope.cardRect(2)) && list->item(1)->data(Qt::UserRole).toInt() == 32,
            "Dragging a wide card lost its span/order.");
    // A drop onto the second cell of the wide card targets that card, not a
    // channel at the same numeric index as the cell.
    start = scope.panelRect(0).topLeft()+QPointF(15,15);
    end = QPointF((scope.cardRect(1).right()+scope.cardRect(2).left())*.5, scope.cardRect(1).center().y());
    mouse(scope, QEvent::MouseButtonPress, start); mouse(scope, QEvent::MouseMove, end); mouse(scope, QEvent::MouseButtonRelease, end); pump(app);
    require(list->item(0)->data(Qt::UserRole).toInt() == 32, "Drop hit testing ignored the internal cell gap of a wide card.");
    scope.present(state, frame, output); scope.findChild<QCheckBox *>("scopeStereo")->setChecked(true);
    scope.grab().save(QDir(directory).filePath("native-span-editor.png"));
    scope.findChild<QPushButton *>("applyLayout")->click(); pump(app); scope.present(state, frame, output); pump(app);
    auto picture = [&] { if (auto gpu = scope.findChild<QQuickWidget *>("scopeGpu")) return gpu->grabFramebuffer(); return scope.grab().toImage(); };
    const auto first = picture(); first.save(QDir(directory).filePath("native-span-cards.png"));
    frame.positionMs += 25; for (auto &v : frame.left[0]) v *= -1; frame.channels[0] = frame.left[0]; ++output.sequence; output.left.fill(0);
    scope.present(state, frame, output); pump(app);
    require(first != picture(), "Spanning cards stopped updating their waveform.");
    // Subsong changes retain spans; resizing the window does not mutate cells.
    ++state.generation; scope.present(state, {}); require(scope.panelRect(32) == scope.cardRect(0).united(scope.cardRect(1)), "Subsong reset a card size.");
    scope.resize(640, 500); pump(app); require(scope.panelRect(32) == scope.cardRect(0).united(scope.cardRect(1)), "Window resize changed the cell span.");
    scope.findChild<QPushButton *>("editLayout")->click(); columns->setValue(1);
    require(scope.panelRect(32) == scope.cardRect(0), "Grid shrink did not clamp a wide card.");
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

#include "window.h"
#include "spectrum.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void pump(QApplication& app, int ms = 70) { QEventLoop loop(&app); QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
void until(QApplication& app, const std::function<bool()>& ready, const char* message) {
    QElapsedTimer timer; timer.start(); while (!ready() && timer.elapsed() < 8000) pump(app, 20); require(ready(), message);
}
void add(ScopeWindow& scope, int source, int view) {
    QTimer::singleShot(0, &scope, [=] {
        auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto sourceBox = dialog->findChild<QComboBox*>("newCardSource");
        sourceBox->setCurrentIndex(sourceBox->findData(source));
        dialog->findChild<QComboBox*>("newCardView")->setCurrentIndex(view);
        dialog->accept();
    });
    scope.findChild<QPushButton*>("addScopeCard")->click();
}
void checkSpectrum(QTextStream& log) {
    QVector<float> pcm(ScopeFrames);
    for (int i = 0; i < pcm.size(); ++i) pcm[i] = 16384*std::sin(2*3.141592653589793*20*i/2048);
    const auto copy = pcm; const auto spectrum = scopeSpectrum(pcm);
    require(pcm == copy && spectrum.size() == 1025, "FFT changed input or size.");
    const int peak = int(std::max_element(spectrum.begin(), spectrum.end())-spectrum.begin());
    require(peak == 20 && std::abs(spectrum[20]+6.0206) < .05, "FFT frequency or dBFS scaling is wrong.");
    pcm.fill(12000); const auto dc = scopeSpectrum(pcm);
    require(*std::max_element(dc.begin(), dc.end()) == -80, "DC displayed as a tone.");
    pcm.fill(0); require(scopeSpectrum(pcm) == dc && scopeSpectrum({}) == dc, "Silence spectrum is not flat.");
    QElapsedTimer timer; timer.start(); for (int i = 0; i < 64; ++i) scopeSpectrum(copy);
    log << "Spectrum: sine-bin frequency, -6 dBFS amplitude, DC/silence and immutable input PASS; 64 transforms " << timer.elapsed() << " ms\n"; log.flush();
}
void checkNotes(const QString& directory, QTextStream& log) {
    const QDir dir(directory);
    std::array<short, BlockFrames*2> a{}, b{};
    for (const auto& name : {"demo.nsf", "named.nsfe"}) {
        GmeTrack plain(dir.filePath(name)), captured(dir.filePath(name), 0, false, true);
        require(captured.info().tonalMask == 7, "NES tonal metadata missing.");
        for (int block = 0; block < 35; ++block) {
            plain.render(a.data(), BlockFrames); captured.render(b.data(), BlockFrames);
            require(a == b, "Note capture changed audible PCM.");
        }
        require(std::abs(captured.noteHz(0)-1789773./(16*254)) < .01 &&
                std::abs(captured.noteHz(2)-1789773./(32*255)) < .01, "NES pulse/triangle pitch incorrect.");
        require(captured.noteHz(1) == 0 && captured.noteHz(3) < 0 && captured.noteHz(4) < 0, "Muted gate or noise/DMC note invented.");
        const auto hz = captured.noteHz(0); captured.seek(0); captured.render(b.data(), BlockFrames);
        require(captured.noteHz(0) == hz, "Note history did not recover after backward seek.");
    }
    // Alternate pulse pitch every ~half second. Small PCM reads deliberately
    // exercise Classic_Emu's much larger internal look-ahead buffer.
    QFile base(dir.filePath("demo.nsf")); require(base.open(QIODevice::ReadOnly), "Cannot read note fixture.");
    auto data = base.readAll();
    const auto routine = QByteArray::fromHex("e600a5002910f004a9a9d002a9fd8d024060");
    qToLittleEndian<quint16>(quint16(0x8000+data.size()-128), data.data()+12);
    data += routine;
    const auto path = dir.filePath("note-steps.nsf"); QFile steps(path); require(steps.open(QIODevice::WriteOnly), "Cannot write note fixture.");
    steps.write(data); steps.close();
    GmeTrack track(path, 0, false, true); track.mute(~1u);
    QVector<float> history; bool low = false, high = false; int compared = 0;
    float previous = -1; int stable = 0;
    for (int i = 0; i < 700; ++i) {
        track.render(a.data(), 128);
        const auto hz = track.noteHz(0); if (hz < 0) continue;
        low |= std::abs(hz-440.4) < 1; high |= std::abs(hz-658.) < 1;
        stable = hz == previous ? stable+1 : 0; previous = hz;
        for (int j = 0; j < 128; ++j) history.push_back(a[j*2]);
        if (history.size() > 2048) history.remove(0, history.size()-2048);
        if (stable >= 17 && history.size() == 2048) {
            const auto bins = scopeSpectrum(history);
            const int peak = int(std::max_element(bins.begin(), bins.end())-bins.begin());
            require(std::abs(peak*SampleRate/2048.-hz) < 22, "Note does not agree with delivered PCM tone.");
            ++compared;
        }
    }
    require(low && high && compared > 100, "Changing notes did not follow the fixture.");
    log << "NES/NSFE notes: hardware periods/gates, noise exclusion, unchanged PCM, rewind and changing pitch vs delivered PCM PASS\n"; log.flush();
}
}
void checkViews(QApplication& app, const QString& directory, QTextStream& log) {
    checkSpectrum(log); checkNotes(directory, log);
    PlayerWindow player; player.player().setVolume(0); player.show(); player.loadFile(QDir(directory).filePath("demo.nsf"));
    auto& scope = player.scopeWindow(); scope.resize(1180, 840); scope.show();
    until(app, [&] { return !player.player().state().busy && player.player().scopes().mask == 31; }, "NES views not ready.");
    scope.findChild<QPushButton*>("editLayout")->click();
    scope.findChild<QSpinBox*>("gridColumns")->setValue(2); scope.findChild<QSpinBox*>("gridRows")->setValue(4);
    auto list = scope.findChild<QListWidget*>("scopeChannelList");
    list->setCurrentRow(0); add(scope, 0, 2); add(scope, 0, 1); pump(app, 250);
    const int keyboard = 33, spectrum = 35;
    require(scope.visibleChannels().contains(keyboard) && scope.visibleChannels().contains(34), "Added cards not visible.");
    require(player.player().scopes().mask == 31, "Duplicate views changed voice capture mask.");
    const auto note = player.player().scopes().noteHz;
    require(note.size() == 5 && note[0] > 400 && note[1] == 0, "Live note snapshot is missing.");
    scope.grab().save(QDir(directory).filePath("native-views-editor.png"));
    scope.findChild<QPushButton*>("applyLayout")->click(); pump(app, 250);
    auto image = [&] {
        auto gpu = dynamic_cast<ScopeGpu*>(scope.findChild<QQuickWidget*>("scopeGpu"));
        return gpu ? gpu->grabFramebuffer() : scope.grab().toImage();
    };
    auto before = image(); require(!before.isNull(), "View framebuffer empty.");
    before.save(QDir(directory).filePath("native-views.png"));
    player.player().setMuteMask(1); pump(app, 160); require(image() != before, "Mute did not dim keyboard/wave cards.");
    scope.findChild<QPushButton*>("editLayout")->click(); add(scope, 32, 1); pump(app, 100);
    require(scope.outputEnabled() && scope.visibleChannels().contains(spectrum), "Added mix did not enable capture.");
    scope.findChild<QPushButton*>("applyLayout")->click();
    player.player().seek(1800, false);
    until(app, [&] { const auto s=player.player().state(); const auto f=player.player().scopes(); return !s.busy && s.positionMs==1800 && f.generation==s.generation && f.mask==31; }, "Views did not recover after paused seek.");
    pump(app, 150); const auto paused = player.player().scopes(); pump(app, 180);
    require(paused.noteHz == player.player().scopes().noteHz && paused.positionMs == player.player().scopes().positionMs, "Paused note state moved.");
    scope.findChild<QPushButton*>("editLayout")->click();
    for (int row=0; row<list->count(); ++row) if (list->item(row)->data(Qt::UserRole).toInt()==spectrum) list->setCurrentRow(row);
    scope.findChild<QComboBox*>("cardView")->setCurrentIndex(2); pump(app);
    require(!scope.outputEnabled(), "Unsupported mix keyboard kept output capture enabled.");
    scope.findChild<QPushButton*>("removeScopeCard")->click(); pump(app);
    require(!scope.visibleChannels().contains(spectrum), "Remove left an added card visible.");
    scope.hide(); pump(app, 120); require(player.player().state().scopesSuspended, "Hidden views continued emulating.");
    scope.show(); player.player().seek(500, true);
    until(app, [&] { return player.player().scopes().mask==31 && player.player().state().positionMs>700; }, "Views did not resume.");
    player.loadFile(QDir(directory).filePath("demo.vgz"));
    until(app, [&] { return !player.player().state().busy && player.player().scopes().mask==255; }, "VGZ failed after note views.");
    require(scope.findChild<QListWidget*>("scopeChannelList")->count()==9, "New file retained old extra cards.");
    scope.findChild<QComboBox*>("cardView")->setCurrentIndex(1); pump(app, 120);
    scope.grab().save(QDir(directory).filePath("native-spectrum-vgz.png"));
    player.loadFile(QDir(directory).filePath("demo.spc"));
    until(app, [&] { return !player.player().state().busy && player.player().scopes().mask==255; }, "SPC failed after note views.");
    scope.findChild<QComboBox*>("cardView")->setCurrentIndex(1); pump(app, 120);
    scope.findChild<QComboBox*>("cardView")->setCurrentIndex(2);
    until(app, [&] { return player.player().scopes().mask==254; }, "Unsupported keyboard unnecessarily retained its voice capture.");
    const auto state = player.player().state(); require(state.starvations == 0 && state.outputErrors == 0, "Views caused audio starvation/errors.");
    player.close();
    log << "Views UI/live: duplicate voice/mix cards, masks, GPU/CPU image, mute dimming, pause/seek, hide/resume, removal, file reset and VGZ/SPC spectra PASS; audio errors 0/0\n"; log.flush();
}

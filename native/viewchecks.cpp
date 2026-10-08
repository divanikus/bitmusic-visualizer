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
#include <QLabel>
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
void checkPitch(QTextStream& log) {
    QVector<float> pcm(ScopeFrames), other(ScopeFrames);
    for (double hz : {55., 110., 440., 1760., 3000.}) {
        for (int i = 0; i < ScopeFrames; ++i) pcm[i] = float(6000*std::sin(2*3.141592653589793*hz*i/SampleRate)+500);
        const auto estimate = scopePitch(pcm);
        require(std::abs(estimate/hz-1) < .03, "PCM pitch estimate missed a sine tone.");
    }
    for (int i = 0; i < ScopeFrames; ++i) {
        pcm[i] = std::fmod(440.*i/SampleRate, 1.) < .25 ? 5000 : -5000;
        other[i] = -pcm[i];
    }
    require(std::abs(scopePitch(pcm, other)-440) < 4, "Harmonic-rich/antiphase stereo pitch is incorrect.");
    const auto copy = pcm;
    QElapsedTimer timer; timer.start(); for (int i = 0; i < 32; ++i) scopePitch(pcm, other);
    require(pcm == copy, "Pitch estimation changed input PCM.");
    log << "PCM pitch: tones, harmonics, DC offset, antiphase stereo PASS; 32 estimates " << timer.elapsed() << " ms\n";
    pcm.fill(0); other.fill(12000); require(scopePitch(pcm, other) == 0, "Silence/DC invented a note.");
    uint32_t random = 17;
    for (auto& value : pcm) { random = random*1664525u+1013904223u; value = float(int(random >> 16)-32768); }
    require(scopePitch(pcm) < 0, "Broadband noise falsely acquired a stable pitch.");
    log << "PCM pitch: silence/noise rejection and immutable samples PASS\n"; log.flush();
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
        const auto hz = track.noteHz(0); require(hz >= 0, "Changing notes intermittently unavailable.");
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
    for (bool pal : {false, true}) {
        auto content = data; content[0x7a] = pal ? 1 : 0;
        QFile longFile(dir.filePath(pal ? "notes-pal.nsf" : "notes-ntsc.nsf"));
        require(longFile.open(QIODevice::WriteOnly), "Cannot write clock fixture."); longFile.write(content); longFile.close();
        GmeTrack continuous(longFile.fileName(), 0, false, true);
        int missing = 0, firstMissing = -1;
        for (int i = 0; i < 180*SampleRate/512; ++i) {
            continuous.render(a.data(), 512);
            if (continuous.noteHz(0) < 0) { ++missing; if (firstMissing < 0) firstMissing = int(continuous.frame()*1000/SampleRate); }
        }
        log << "Continuous " << (pal ? "PAL" : "NTSC") << " notes (180 seconds): missing=" << missing << " firstMs=" << firstMissing << '\n'; log.flush();
        require(!missing, "Continuous NES notes intermittently unavailable.");
        for (qint64 position : {SampleRate*20LL, SampleRate/3LL, SampleRate*60LL}) {
            continuous.seek(position); continuous.render(a.data(), 128);
            require(continuous.noteHz(0) >= 0, "Note clock did not recover after seek.");
        }
    }
    log << "NES/NSFE notes: hardware periods/gates, noise exclusion, unchanged PCM, rewind and changing pitch vs delivered PCM PASS\n"; log.flush();
}
}
void checkViews(QApplication& app, const QString& directory, QTextStream& log) {
    checkSpectrum(log); checkPitch(log); checkNotes(directory, log);
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
    require(scope.outputEnabled() && scope.findChild<QLabel*>("keyboardHint")->text().contains("Estimated"), "Mix keyboard did not enable/explain its estimate.");
    scope.findChild<QPushButton*>("removeScopeCard")->click(); pump(app);
    require(!scope.visibleChannels().contains(spectrum) && !scope.outputEnabled(), "Remove left an added card visible/capturing.");
    list->setCurrentRow(0); // Original waveform, with two other views of its source.
    require(list->currentItem()->data(Qt::UserRole).toInt() == 0, "Wrong original card selected.");
    auto remove = scope.findChild<QPushButton*>("removeScopeCard"); require(remove->isEnabled(), "Remove disabled for an original card.");
    remove->click(); pump(app);
    require(!scope.visibleChannels().contains(0) && player.player().scopes().mask==31, "Removing original card affected a duplicate or its capture.");
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
    require(player.player().scopes().mask==255 && scope.findChild<QLabel*>("keyboardHint")->text().contains("Estimated"), "SPC keyboard missing source capture/estimate label.");
    pump(app, 100); scope.grab().save(QDir(directory).filePath("native-keyboard-estimated.png"));
    const int count = list->count();
    for (int i=0; i<count; ++i) { list->setCurrentRow(0); remove->click(); }
    require(list->count()==0 && !remove->isEnabled() && scope.visibleChannels().isEmpty(), "Remove could not clear all cards.");
    add(scope, 0, 2); pump(app);
    require(list->count()==1 && scope.visibleChannels().size()==1, "Deleted source could not be re-added.");
    scope.findChild<QPushButton*>("resetLayout")->click(); pump(app);
    require(list->count()==9 && scope.visibleChannels().size()==8, "Reset did not restore original cards.");
    const auto state = player.player().state(); require(state.starvations == 0 && state.outputErrors == 0, "Views caused audio starvation/errors.");
    player.close();
    log << "Views UI/live: duplicate voice/mix cards, masks, GPU/CPU image, mute dimming, pause/seek, hide/resume, removal, file reset and VGZ/SPC spectra PASS; audio errors 0/0\n"; log.flush();
}

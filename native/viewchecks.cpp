#include "playerwindow.h"
#include "spectrum.h"
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
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

void checkPsgNotes(QApplication&, const QString&, QTextStream&);
void checkGbAyNotes(QApplication&, const QString&, QTextStream&);
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
void checkAnalysisCache(QTextStream &log) {
    ScopeAnalysis analysis;
    QVector<float> low(ScopeFrames), high(ScopeFrames);
    for (int i = 0; i < ScopeFrames; ++i) {
        low[i] = float(4000 * std::sin(2 * 3.141592653589793 * 20 * i / 2048));
        high[i] = float(16000 * std::sin(2 * 3.141592653589793 * 40 * i / 2048));
    }
    ScopeAnalysisKey key{7, 0, 100, false};
    WaveOptions options; options.scale = WaveOptions::Instant; options.trigger = WaveOptions::Off;
    const auto firstWave = analysis.waveform(0, key, low, low, options);
    require(analysis.waveform(0, key, high, high, options).peak == firstWave.peak,
            "A repaint/duplicate recalculated waveform analysis for the same snapshot.");
    const auto firstBins = analysis.spectrum(0, key, low, low).left;
    require(analysis.spectrum(0, key, high, high).left == firstBins,
            "A repaint/duplicate recalculated spectrum analysis for the same snapshot.");
    const auto mixBins = analysis.spectrum(VoiceMask::Capacity, key, high, high).left;
    require(mixBins != firstBins && analysis.spectrum(0, key, low, low).left == firstBins,
            "Full mix analysis aliases a voice cache.");
    ++key.sequence;
    require(analysis.waveform(0, key, high, high, options).peak > firstWave.peak * 3 &&
            analysis.spectrum(0, key, high, high).left == mixBins,
            "A new output sequence at the same position did not update analysis.");
    key.stereo = true;
    const auto stereo = analysis.spectrum(0, key, low, high);
    require(stereo.left == firstBins && stereo.right == mixBins, "Stereo analysis did not retain independent sides.");
    require(analysis.waveform(0, key, low, low, options).peak == firstWave.peak,
            "Changing stereo mode retained the old waveform state at the same position.");
    ++key.generation;
    require(analysis.waveform(0, key, high, high, options).peak > firstWave.peak * 3,
            "Changing generation retained the old waveform state at the same position.");
    require(analysis.spectrum(0, key, high, low).left == mixBins,
            "A new generation at the same position retained old analysis.");
    analysis.clearWave(0);
    require(analysis.waveform(0, key, low, low, options).peak == firstWave.peak,
            "Wave invalidation failed to discard old envelope/trigger state.");

    QVector<float> a(ScopeFrames), b(ScopeFrames);
    for (int i = 0; i < ScopeFrames; ++i) {
        a[i] = float(6000 * std::sin(2 * 3.141592653589793 * 440 * i / SampleRate));
        b[i] = float(6000 * std::sin(2 * 3.141592653589793 * 880 * i / SampleRate));
    }
    key = {9, 1, 100, false}; analysis.estimatePitch(69, key, a, {});
    const float pitch = analysis.pitch(69, key.generation);
    require(std::abs(pitch - 440) < 4, "Pitch cache missed its initial signal.");
    key.position = 132; ++key.sequence; analysis.estimatePitch(69, key, b, {});
    require(analysis.pitch(69, key.generation) == pitch, "Pitch analysis exceeded its established cadence.");
    key.position = 133; analysis.estimatePitch(69, key, b, {});
    require(std::abs(analysis.pitch(69, key.generation) - 880) < 8, "Pitch did not update at the cadence boundary.");
    key.position = 50; analysis.estimatePitch(69, key, a, {});
    require(std::abs(analysis.pitch(69, key.generation) - 440) < 4, "A rewind retained a future pitch estimate.");
    ++key.generation; analysis.estimatePitch(69, key, b, {});
    require(std::abs(analysis.pitch(69, key.generation) - 880) < 8 && analysis.pitch(69, key.generation - 1) < 0,
            "Pitch generations were mixed or a generation change was throttled.");
    analysis.clearWaves();
    require(analysis.pitch(69, key.generation) > 0 && analysis.spectrum(0, {8, 1, 100, true}, low, high).left == mixBins,
            "Wave-only invalidation discarded unrelated analysis.");
    analysis.clear();
    require(analysis.pitch(69, key.generation) < 0 && analysis.spectrum(0, {8, 1, 100, true}, low, high).left == firstBins,
            "Full invalidation retained source analysis.");
    log << "Source analysis cache: duplicate reuse, source/mix isolation, sequence/stereo/generation invalidation, pitch cadence/rewind and scoped resets PASS\n";
}
void checkPlotGeometry(QTextStream &log) {
    const QRectF plot(12, 8, 400, 120);
    ScopeWave wave;
    QVector<float> pcm(ScopeFrames);
    for (int i = 0; i < pcm.size(); ++i) pcm[i] = float(i - 2048);
    const auto copy = pcm;
    const auto mono = scopeWavePoints(pcm, wave, plot, false, 0);
    const auto left = scopeWavePoints(pcm, wave, plot, true, 0);
    const auto right = scopeWavePoints(pcm, wave, plot, true, 1);
    require(mono.size() == 800 && mono.first().x() == plot.left() && mono.last().x() == plot.right() &&
            std::abs(mono.first().y() - (plot.center().y() + 1024 * (float(plot.height() * .44) / wave.peak))) < 1e-5,
            "Waveform plot changed its sample window, point density or amplitude.");
    require(left.size() == right.size() && std::abs(right.first().y() - left.first().y() - plot.height() * .5) < 1e-5 && pcm == copy,
            "Stereo waveform lanes lost their shared trigger or changed PCM.");
    require(scopeWavePoints(pcm, wave, {0, 0, 2000, 100}, false, 0).size() == 2048,
            "Waveform geometry exceeded its existing point bound.");
    const auto bounds = scopeSpectrumBounds(plot, false, 0);
    const auto noGrid = scopeSpectrumBounds(plot, false, 0, false);
    const auto upper = scopeSpectrumBounds(plot, true, 0), lower = scopeSpectrumBounds(plot, true, 1);
    require(bounds.top == plot.top() && bounds.bottom == plot.bottom() - 16 && noGrid.bottom == bounds.bottom + 14 &&
            lower.top - upper.top == plot.height() * .5, "Spectrum lane/grid margins changed.");
    QVector<float> bins(1025, -20);
    const auto line = scopeSpectrumLine(bins, plot, bounds);
    const double y = bounds.bottom - bounds.height * .75;
    require(line.size() == 400 && line.first() == QPointF(plot.left(), y) && line.last() == QPointF(plot.right(), y),
            "Spectrum line changed its endpoints or dBFS height.");
    const auto bars = scopeSpectrumBars(bins, {plot.left(), bounds.top + 5, plot.width(), bounds.height});
    require(!bars.isEmpty() && std::abs(bars.first().top() - y) < 1e-8,
            "Spectrum lines and bars use different peak/height mappings.");
    log << "Plot geometry: waveform sample window/density/stereo, immutable PCM, spectrum lane margins and line/bar mapping PASS\n";
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
    require(scopeSpectrumBars(dc, {0, 0, 400, 100}).isEmpty(), "Silent spectrum has bars.");
    QVector<float> peakBins(1025, -80); peakBins[20] = -20;
    for (double width : {2., 50., 400., 2000.}) {
        const QRectF area(12, 8, width, 120);
        const auto bars = scopeSpectrumBars(peakBins, area);
        require(!bars.isEmpty() && bars.size() <= 96, "Bar bands lost a narrow FFT peak or exceeded their bound.");
        for (const auto &bar : bars) require(area.contains(bar) && std::abs(bar.height()-90) < .01,
            "Spectrum bar is outside the plot or has the wrong dBFS height.");
    }
    QElapsedTimer timer; timer.start(); for (int i = 0; i < 64; ++i) scopeSpectrum(copy);
    log << "Spectrum: frequency/dBFS, silence, immutable input, bar peak preservation and bounds PASS; 64 transforms " << timer.elapsed() << " ms\n"; log.flush();
}
void checkSpectrumControls(QApplication& app, const QString& directory, QTextStream& log) {
    ScopeWindow scope; scope.resize(1050, 650); scope.show();
    PlayerState state; state.valid = true; state.generation = 1;
    state.info.path = "spectrum-preview"; state.info.title = "Spectrum styles";
    state.info.voices = {"Pulse", "Mix"}; state.info.stereoOutput = true;
    ScopeFrame frame; frame.generation = 1; frame.mask = 3;
    QVector<float> row(ScopeFrames), right(ScopeFrames);
    for (int i = 0; i < ScopeFrames; ++i) {
        row[i] = float(5000*std::sin(i*.055)+2200*std::sin(i*.23)+1100*std::sin(i*.71));
        right[i] = float(4200*std::sin(i*.11)+2100*std::sin(i*.45));
    }
    frame.channels = {row, row}; frame.left = {row, row}; frame.right = {right, right};
    scope.present(state, frame); scope.findChild<QPushButton*>("editLayout")->click(); pump(app);
    auto list = scope.findChild<QListWidget*>("scopeChannelList");
    auto view = scope.findChild<QComboBox*>("cardView");
    auto style = scope.findChild<QComboBox*>("spectrumStyle");
    auto grid = scope.findChild<QCheckBox*>("spectrumGrid");
    list->setCurrentRow(0); require(!style->isVisible(), "Spectrum controls shown for a waveform.");
    view->setCurrentIndex(1); pump(app);
    require(style->isVisible() && style->currentIndex() == 0 && grid->isChecked(), "Spectrum defaults/controls missing.");
    auto capture = [&] {
        pump(app, 90);
        auto gpu = dynamic_cast<ScopeGpu*>(scope.findChild<QQuickWidget*>("scopeGpu"));
        auto image = gpu ? gpu->grabFramebuffer() : scope.grab().toImage();
        const auto r = scope.panelRect(0); const auto dpr = scope.devicePixelRatioF();
        return image.copy(QRect(qRound(r.x()*dpr), qRound(r.y()*dpr), qRound(r.width()*dpr), qRound(r.height()*dpr)));
    };
    const auto axes = capture(); grid->setChecked(false); const auto line = capture();
    require(axes != line, "Grid toggle did not redraw paused spectrum.");
    style->setCurrentIndex(1); const auto bars = capture();
    require(bars != line, "Bars did not replace paused spectrum line.");
    bars.save(QDir(directory).filePath("native-spectrum-bars.png"));
    list->setCurrentRow(1); view->setCurrentIndex(1);
    require(style->currentIndex()==0 && grid->isChecked(), "Spectrum settings leaked to another card.");
    list->setCurrentRow(0); require(style->currentIndex()==1 && !grid->isChecked(), "Card settings were lost on selection.");
    view->setCurrentIndex(2); require(!style->isVisible(), "Spectrum controls shown for Keyboard.");
    view->setCurrentIndex(1); require(style->currentIndex()==1 && !grid->isChecked(), "Switching views lost spectrum options.");
    scope.findChild<QCheckBox*>("scopeStereo")->setChecked(true); pump(app);
    scope.grab().save(QDir(directory).filePath("native-spectrum-bars-editor.png"));
    scope.findChild<QPushButton*>("applyLayout")->click(); pump(app);
    scope.grab().save(QDir(directory).filePath("native-spectrum-styles.png"));
    scope.hide(); scope.show(); scope.present(state, frame); pump(app);
    scope.findChild<QPushButton*>("editLayout")->click();
    require(style->currentIndex()==1 && !grid->isChecked(), "Hide/show lost spectrum options.");
    scope.resize(620, 390); pump(app);
    scope.findChild<QPushButton*>("resetLayout")->click(); list->setCurrentRow(0); view->setCurrentIndex(1);
    require(style->currentIndex()==0 && grid->isChecked(), "Reset did not restore spectrum defaults.");
    style->setCurrentIndex(1); grid->setChecked(false);
    scope.beginFile(); state.info.path = "next-spectrum-preview"; scope.present(state, frame);
    list->setCurrentRow(0); view->setCurrentIndex(1);
    require(style->currentIndex()==0 && grid->isChecked(), "New file retained previous spectrum options.");
    scope.close(); log << "Spectrum UI: conditional/per-card controls, paused redraw, stereo, hide/resize, Reset and new-file defaults PASS\n"; log.flush();
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
    checkGbAyNotes(app, directory, log);
    checkAnalysisCache(log);
    checkPlotGeometry(log);
    checkSpectrum(log); checkSpectrumControls(app, directory, log); checkPitch(log); checkNotes(directory, log);
    checkPsgNotes(app, directory, log);
    PlayerWindow player; player.player().setVolume(0); player.show(); player.loadFile(QDir(directory).filePath("demo.nsf"));
    auto& scope = player.scopeWindow(); scope.resize(1180, 840); scope.show();
    until(app, [&] { return !player.player().state().busy && player.player().scopes().mask == 31; }, "NES views not ready.");
    scope.findChild<QPushButton*>("editLayout")->click();
    scope.findChild<QSpinBox*>("gridColumns")->setValue(2); scope.findChild<QSpinBox*>("gridRows")->setValue(4);
    auto list = scope.findChild<QListWidget*>("scopeChannelList");
    list->setCurrentRow(0); add(scope, 0, 2); add(scope, 0, 1); pump(app, 250);
    const int keyboard = ScopeTheme::FullMix+1, spectrum = ScopeTheme::FullMix+3;
    require(scope.visibleChannels().contains(keyboard) && scope.visibleChannels().contains(keyboard+1), "Added cards not visible.");
    require(player.player().scopes().mask == 31, "Duplicate views changed voice capture mask.");
    const auto note = player.player().scopes().noteHz;
    require(note.size() == 5 && note[0].value(0) > 400 && note[1].isEmpty(), "Live note snapshot is missing.");
    scope.grab().save(QDir(directory).filePath("native-views-editor.png"));
    scope.findChild<QPushButton*>("applyLayout")->click(); pump(app, 250);
    auto image = [&] {
        auto gpu = dynamic_cast<ScopeGpu*>(scope.findChild<QQuickWidget*>("scopeGpu"));
        return gpu ? gpu->grabFramebuffer() : scope.grab().toImage();
    };
    auto before = image(); require(!before.isNull(), "View framebuffer empty.");
    before.save(QDir(directory).filePath("native-views.png"));
    player.player().setMuteMask(1); pump(app, 160); require(image() != before, "Mute did not dim keyboard/wave cards.");
    scope.findChild<QPushButton*>("editLayout")->click(); add(scope, ScopeTheme::FullMix, 1); pump(app, 100);
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

#include "window.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>
#include <QtEndian>
#include <zlib.h>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void pump(QApplication& app, int ms = 60) { QEventLoop loop(&app); QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
void until(QApplication& app, const std::function<bool()>& ready) {
    QElapsedTimer timer; timer.start(); while (!ready() && timer.elapsed() < 8000) pump(app, 20);
    require(ready(), "PSG keyboard transport timed out.");
}
QByteArray tune(int fm) {
    QByteArray data(0x40, 0); data.replace(0, 4, "Vgm ");
    auto word = [&](int at, quint32 value) { qToLittleEndian(value, data.data()+at); };
    word(8, 0x150); word(12, 3579545); word(0x34, 12);
    if (fm) word(fm == 1 ? 44 : 16, fm == 1 ? 7670454 : 3579545);
    auto psg = [&](int value) { data.append(char(0x50)); data.append(char(value)); };
    auto tone = [&](int voice, int divisor) { psg(0x80+voice*32+(divisor&15)); psg(divisor>>4); };
    auto volume = [&](int voice, int level) { psg(0x90+voice*32+level); };
    auto wait = [&] { data.append(char(0x61)); data.append(char(0xe8)); data.append(char(0x44)); }; // 400 ms
    auto stereo = [&](int mask) { data.append(char(0x4f)); data.append(char(mask)); };
    stereo(255); tone(0, 428); tone(1, 339); tone(2, 285);
    for (int voice = 0; voice < 4; ++voice) volume(voice, 4);
    psg(0xe4); wait(); // C4 + E4 + G4, with independent noise
    volume(1, 15); wait();
    volume(0, 15); volume(2, 15); wait(); // noise alone
    for (int voice = 0; voice < 3; ++voice) { tone(voice, 428); volume(voice, 4); }
    wait(); // unison
    stereo(0); wait();
    stereo(255); for (int voice = 0; voice < 3; ++voice) tone(voice, 4);
    wait(); // inaudible periods, suppressed by the core
    tone(0, 428); tone(1, 339); tone(2, 285); wait();
    data.append(char(0x66)); word(4, quint32(data.size()-4));
    word(0x18, 7*17640); word(0x1c, 0x40-0x1c); word(0x20, 7*17640);
    return data;
}
void write(const QString& path, const QByteArray& data) {
    QFile f(path); require(f.open(QIODevice::WriteOnly) && f.write(data)==data.size(), "Cannot write PSG test fixture.");
}
QByteArray gzip(const QByteArray& data) {
    z_stream stream{}; require(deflateInit2(&stream, 6, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY)==Z_OK, "gzip init failed.");
    QByteArray result(int(deflateBound(&stream, uLong(data.size()))), 0);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData())); stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef*>(result.data()); stream.avail_out = uInt(result.size());
    const int status = deflate(&stream, Z_FINISH); result.resize(qsizetype(stream.total_out)); deflateEnd(&stream);
    require(status == Z_STREAM_END, "gzip fixture failed."); return result;
}
QVector<float> tones(GmeTrack& track, int fm) {
    if (fm) return track.notePitches(fm == 1 ? 7 : 9);
    QVector<float> result;
    for (int ch = 0; ch < 3; ++ch) result += track.notePitches(ch);
    return result;
}
QVector<float> expected(int phase) {
    const float c = 3579545.f/(32*428), e = 3579545.f/(32*339), g = 3579545.f/(32*285);
    if (phase == 0 || phase == 6) return {c, e, g};
    if (phase == 1) return {c, g};
    if (phase == 3) return {c, c, c};
    return {};
}
void checkGenericKeyboard(QApplication& app, const QDir& dir) {
    ScopeWindow scope; scope.resize(1000, 470); scope.show();
    PlayerState state; state.valid = true; state.generation = 1;
    state.info.path = "polyphony-preview"; state.info.title = "Polyphonic keyboard";
    state.info.voices = {"Chord"}; state.info.tonalMask = 1;
    ScopeFrame frame; frame.generation = 1; frame.mask = 1;
    frame.channels = {QVector<float>(ScopeFrames)}; frame.noteHz = {{}};
    scope.present(state, frame); scope.findChild<QPushButton*>("editLayout")->click();
    scope.findChild<QListWidget*>("scopeChannelList")->setCurrentRow(0);
    scope.findChild<QComboBox*>("cardView")->setCurrentIndex(2);
    scope.findChild<QPushButton*>("applyLayout")->click(); pump(app);
    auto image = [&] {
        pump(app); auto gpu = dynamic_cast<ScopeGpu*>(scope.findChild<QQuickWidget*>("scopeGpu"));
        return gpu ? gpu->grabFramebuffer() : scope.grab().toImage();
    };
    const auto silent = image();
    auto keyPixel = [&](const QImage& image, int midi) {
        const auto keys = scope.panelRect(0).adjusted(12, 55, -12, -10);
        int white = 0;
        for (int n = 24; n < midi; ++n) if (n%12!=1 && n%12!=3 && n%12!=6 && n%12!=8 && n%12!=10) ++white;
        const double dpr = scope.devicePixelRatioF();
        return image.pixelColor(int((keys.left()+keys.width()*(white+.5)/49)*dpr), int((keys.top()+keys.height()*.8)*dpr));
    };
    // The widget accepts more than three notes, independently of the PSG backend.
    frame.noteHz = {{261.63f, 293.66f, 329.63f, 392.f, 440.f, 261.63f}};
    scope.present(state, frame); const auto chord = image();
    for (int midi : {60, 62, 64, 67, 69}) require(keyPixel(silent, midi)!=keyPixel(chord, midi), "Polyphonic keyboard missed a key.");
    require(keyPixel(silent, 65)==keyPixel(chord, 65), "Polyphonic keyboard lit an unrelated key.");
    chord.save(dir.filePath("native-keyboard-chord.png"));
    frame.noteHz = {{261.63f}}; scope.present(state, frame); const auto single = image();
    frame.noteHz = {{261.63f, 261.63f, 261.63f}}; scope.present(state, frame);
    require(single == image(), "Unison pitches produced duplicate keys/readout.");
    state.muteMask = 1; scope.present(state, frame); require(single!=image(), "Muted chord not dimmed.");
    state.muteMask = 0; frame.noteHz = {{}}; scope.present(state, frame);
    require(keyPixel(silent, 60)==keyPixel(image(), 60), "Silence retained a pressed key.");
    scope.close();
}
}

void checkPsgNotes(QApplication& app, const QString& directory, QTextStream& log) {
    const QDir dir(directory);
    for (int fm = 0; fm < 3; ++fm) {
        const auto path = dir.filePath(QString("psg-chords-%1.vgm").arg(fm)); write(path, tune(fm));
        GmeTrack plain(path), captured(path, 0, false, true);
        require(captured.info().tonalMask == (fm ? 1u << (fm == 1 ? 7 : 9) : 7u), "PSG capability mapping incorrect.");
        std::array<short, BlockFrames*2> a{}, b{};
        int checked = 0;
        for (int i = 0; i < (fm ? 12 : 180)*SampleRate/512; ++i) {
            plain.render(a.data(), 512); captured.render(b.data(), 512);
            require(a == b, "PSG note capture changed audio.");
            const auto notes = tones(captured, fm);
            require(!notes.contains(-1), "PSG notes intermittently unavailable.");
            const int ms = int(captured.frame()*1000/SampleRate);
            // Long runs check continuity; VGM clock quantization can shift the
            // musical event times slightly relative to nominal milliseconds.
            if (ms < 12000 && ms%400 > 20 && ms%400 < 380) {
                const auto target = expected((ms/400)%7);
                require(notes == target, "PSG notes do not follow delivered audio/chip gates."); ++checked;
            }
        }
        require(checked > 100, "Too few PSG clock checks.");
        for (int ms : {1300, 200, 900, 3000}) {
            captured.seek(qint64(ms)*SampleRate/1000); captured.render(b.data(), 128);
            require(tones(captured, fm)==expected((ms/400)%7), "PSG notes lost on seek/loop.");
        }
        captured.seek(0); captured.mute(~0u); captured.render(b.data(), 512);
        require(tones(captured, fm).size()==3, "User mute erased chip notes.");
        // Same source path also works when decompressed from VGZ.
        const auto packed = path + ".vgz"; write(packed, gzip(tune(fm)));
        GmeTrack zipped(packed, 0, false, true); zipped.render(b.data(), 512);
        require(tones(zipped, fm).size()==3, "VGZ notes missing.");
        log << "PSG notes mode " << fm << ": unchanged PCM, chords/gates/unison/routing, continuous clocks, seeks/loops and VGZ PASS\n"; log.flush();
        PlayerWindow player; player.player().setVolume(0); player.show(); player.loadFile(packed);
        auto& scope = player.scopeWindow(); scope.show();
        const int source = fm == 1 ? 7 : fm == 2 ? 9 : 0;
        until(app, [&] { return !player.player().state().busy && (player.player().scopes().mask & (1u<<source)); });
        scope.findChild<QPushButton*>("editLayout")->click();
        scope.findChild<QListWidget*>("scopeChannelList")->setCurrentRow(source);
        scope.findChild<QComboBox*>("cardView")->setCurrentIndex(2);
        require(!scope.findChild<QLabel*>("keyboardHint")->text().contains("Estimated"), "PSG keyboard used an audio estimate.");
        player.player().seek(200, false);
        until(app, [&] { const auto s=player.player().state(); return !s.busy && s.positionMs==200 && player.player().scopes().generation==s.generation; });
        require(player.player().scopes().noteHz.value(source).size()==(fm ? 3 : 1), "Shared/solo scope publication lost pitches.");
        pump(app); scope.grab().save(dir.filePath(QString("native-psg-keyboard-%1.png").arg(fm)));
        scope.hide(); pump(app); scope.show(); player.player().seek(900, false);
        until(app, [&] { const auto s=player.player().state(); return !s.busy && s.positionMs==900 && player.player().scopes().generation==s.generation; });
        require(player.player().scopes().noteHz.value(source).isEmpty(), "Paused seek retained old PSG keys.");
        player.close();
    }
    checkGenericKeyboard(app, dir);
    log << "Keyboard lists: five simultaneous keys, duplicate unison, silence, dimming and live shared/solo publication PASS\n"; log.flush();
}

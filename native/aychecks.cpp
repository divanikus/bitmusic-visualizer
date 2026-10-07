#include "window.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
using Block = std::array<short, BlockFrames * 2>;
int peak(const Block &pcm) {
    int result = 0; for (auto s : pcm) result = std::max(result, std::abs(int(s))); return result;
}
float peak(const QVector<float> &pcm) {
    float result = 0; for (auto s : pcm) result = std::max(result, std::abs(s)); return result;
}
void until(QApplication &app, const std::function<bool()> &ready, const char *message) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 10000) { app.processEvents(); QThread::msleep(4); }
    require(ready(), message);
}
QString fixture(const QString &directory) {
    // Original ZXAYEMUL container: an AY three-tone driver and a 48K beeper loop.
    QByteArray ay;
    auto reg = [&](int address, int value) {
        ay += QByteArray::fromHex("01fdff3e"); ay += char(address); ay += QByteArray::fromHex("ed7906bf3e");
        ay += char(value); ay += QByteArray::fromHex("ed79");
    };
    reg(0, 180); reg(1, 0); reg(2, 70); reg(3, 1); reg(4, 150); reg(5, 2);
    reg(7, 0x38); reg(8, 15); reg(9, 12); reg(10, 10); ay += char(0xc9);
    const QByteArray beep = QByteArray::fromHex("3e10d3fe064010feafd3fe064010fec30080");
    QByteArray data(0x80, '\0'); data.replace(0, 8, "ZXAYEMUL"); data[16] = 1;
    auto word = [&](int at, int value) { qToBigEndian<quint16>(value, data.data() + at); };
    auto pointer = [&](int at, int target) { word(at, target - at); };
    auto append = [&](const QByteArray &bytes) { int at = data.size(); data += bytes; return at; };
    pointer(18, 0x20);
    for (int song = 0; song < 2; ++song) {
        const int track = 0x30 + 0x10 * song, more = 0x50 + 6 * song, blocks = 0x60 + 0x10 * song;
        const auto name = song ? QByteArray("Beeper test") : QByteArray("AY three tones");
        const int nameAt = append(name + '\0'); pointer(0x20 + 4 * song, nameAt); pointer(0x22 + 4 * song, track);
        word(track + 4, 100); // Two seconds, used to exercise automatic repeat.
        pointer(track + 10, more); pointer(track + 12, blocks);
        word(more, 0xff00); word(more + 2, 0x8000);
        word(more + 4, song ? 0 : 0x8000 + ay.size() - 1);
        const auto &code = song ? beep : ay; const int codeAt = append(code);
        word(blocks, 0x8000); word(blocks + 2, code.size()); pointer(blocks + 4, codeAt);
    }
    const auto path = QDir(directory).filePath("native-spectrum.ay");
    QFile file(path); require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "Cannot write AY fixture.");
    return path;
}
}

void checkAy(QApplication &app, const QString &directory, QTextStream &log) {
    const auto path = fixture(directory);
    GmeTrack master(path);
    require(master.info().voices == QStringList({"AY A", "AY B", "AY C", "Beeper"}) && !master.info().stereoOutput,
        "AY channel identity/mono capability incorrect.");
    require(master.info().songs == 2 && master.info().playlist[1].title == "Beeper test" && master.info().durationMs == 2000,
        "AY subsong metadata incorrect.");
    Block pcm{}, reference{}; std::array<Block, 3> ayVoices;
    for (int song = 0; song < 2; ++song) {
        for (int voice = 0; voice < 4; ++voice) {
            GmeTrack solo(path, song); solo.mute(~(1u << voice));
            solo.seek(SampleRate / 2); solo.render(pcm.data(), BlockFrames);
            const bool active = song ? voice == 3 : voice < 3;
            require(active ? peak(pcm) > 100 : peak(pcm) <= 1, "AY/beeper solo isolation failed.");
            for (size_t i = 0; i < pcm.size(); i += 2) require(pcm[i] == pcm[i + 1], "AY source is not mono.");
            if (!song && voice < 3) ayVoices[voice] = pcm;
            solo.seek(3 * SampleRate); solo.seek(SampleRate / 2); solo.render(reference.data(), BlockFrames);
            require(pcm == reference, "AY/beeper rewind changed waveform or solo mask.");
        }
        GmeTrack muted(path, song); muted.mute(15); muted.seek(SampleRate); muted.render(pcm.data(), BlockFrames);
        require(peak(pcm) <= 1, "AY/beeper all-mute failed.");
        muted.mute(0); for (int i = 0; i < 8; ++i) muted.render(pcm.data(), BlockFrames);
        require(peak(pcm) > 100, "AY/beeper unmute failed.");
    }
    require(ayVoices[0] != ayVoices[1] && ayVoices[0] != ayVoices[2] && ayVoices[1] != ayVoices[2], "AY voices duplicated.");
    log << "AY: three distinct chip voices plus beeper, solo/mute/unmute, mono PCM, metadata and deterministic rewind PASS\n"; log.flush();

    PlayerWindow window; window.player().setVolume(0); window.show(); window.loadFile(path);
    auto ready = [&] {
        const auto s = window.player().state(); const auto f = window.player().scopes();
        return s.valid && !s.busy && f.generation == s.generation && f.mask == 15 && std::abs(s.positionMs - f.positionMs) < 200;
    };
    until(app, ready, "AY scopes did not prepare."); window.refresh();
    auto stereo = window.scopeWindow().findChild<QCheckBox *>("scopeStereo"); stereo->setChecked(true);
    require(!stereo->isEnabled() && window.findChild<QListWidget *>("songs")->item(1)->text() == "Beeper test",
        "AY mono UI or playlist incorrect.");
    for (int song = 0; song < 2; ++song) {
        window.loadFile(path, song); until(app, [&] { return ready() && window.player().state().info.song == song; }, "AY subsong load failed.");
        for (int position : {1200, 250, 1000}) {
            window.player().seek(position, false); until(app, ready, "AY paused seek stuck Preparing.");
            const auto frame = window.player().scopes();
            require(!window.player().state().playing && frame.positionMs == position, "AY paused seek clock incorrect.");
            for (int voice = 0; voice < 4; ++voice)
                require((song ? voice == 3 : voice < 3) ? peak(frame.channels[voice]) > 100 : peak(frame.channels[voice]) <= 1,
                    "AY scope lost active/silent channel isolation after seek.");
        }
        window.player().setRepeat(true); window.player().seek(1850, true);
        const auto generation = window.player().state().generation;
        until(app, [&] { return window.player().state().generation != generation && ready(); }, "AY repeat scopes did not recover.");
        require(window.player().state().info.song == song && window.player().state().playing, "AY repeat changed/stopped subsong.");
        const auto frame = window.player().scopes();
        require(peak(frame.channels[song ? 3 : 0]) > 100 && peak(frame.channels[song ? 0 : 3]) <= 1,
            "AY repeat lost voice isolation.");
        window.player().setRepeat(false);
    }
    window.refresh(); window.scopeWindow().grab().save(QDir(directory).filePath("native-ay-beeper.png"));
    window.findChild<QPushButton *>("stop")->click();
    until(app, [&] { const auto s = window.player().state(); return !s.busy && !s.playing && s.positionMs == 0; }, "AY Stop failed.");
    const auto state = window.player().state();
    require(state.error.isEmpty() && state.scopeError.isEmpty() && !state.starvations && !state.outputErrors, "AY audio/scope errors.");
    window.close();
    log << "AY UI: named subsongs, mono scopes, paused forward/backward seeks, AY/beeper repeat recovery and Stop; audio errors=0 PASS\n"; log.flush();
}

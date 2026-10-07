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
int sidePeak(const Block &pcm, int side) {
    int peak = 0;
    for (size_t i = side; i < pcm.size(); i += 2) peak = std::max(peak, std::abs(int(pcm[i])));
    return peak;
}
void until(QApplication &app, const std::function<bool()> &ready, const char *message) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 10000) { app.processEvents(); QThread::msleep(4); }
    require(ready(), message);
}
QString fixture(const QString &directory) {
    // Original LR35902 driver: four continuous tones, with reversed routing in song 2.
    QByteArray code = QByteArray::fromHex("f5"); // Preserve the zero-based subsong in A.
    auto reg = [&](int address, int value) {
        code += char(0x3e); code += char(value); code += char(0xe0); code += char(address);
    };
    reg(0x26, 0); reg(0x26, 0x80); reg(0x24, 0x77);
    reg(0x10, 0); reg(0x11, 0x80); reg(0x12, 0xf0); reg(0x13, 0xd6); reg(0x14, 0x86);
    reg(0x16, 0x40); reg(0x17, 0xa0); reg(0x18, 0x83); reg(0x19, 0x87);
    reg(0x1a, 0);
    for (int i = 0; i < 16; ++i) reg(0x30 + i, i * 17);
    reg(0x1a, 0x80); reg(0x1b, 0); reg(0x1c, 0x20); reg(0x1d, 0x40); reg(0x1e, 0x87);
    reg(0x20, 0); reg(0x21, 0x80); reg(0x22, 0x35); reg(0x23, 0x80);
    code += QByteArray::fromHex("f1b728043ea518023e5ae025c9");
    const int playAddress = 0x400 + code.size(); code += char(0xc9); // Play just returns.
    QByteArray data(0x70, '\0'); data.replace(0, 6, QByteArray::fromHex("474253010201"));
    qToLittleEndian<quint16>(0x400, data.data() + 6);
    qToLittleEndian<quint16>(0x400, data.data() + 8);
    qToLittleEndian<quint16>(playAddress, data.data() + 10);
    qToLittleEndian<quint16>(0xfffe, data.data() + 12);
    const QByteArray title("Original four-voice stereo test"); data.replace(16, title.size(), title);
    data += code;
    const auto path = QDir(directory).filePath("native-stereo.gbs");
    QFile file(path); require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "Cannot write GBS fixture.");
    return path;
}
}

void checkGbs(QApplication &app, const QString &directory, QTextStream &log) {
    const auto path = fixture(directory);
    GmeTrack master(path, 0, true, true);
    require(master.info().voices == QStringList({"Square 1", "Square 2", "Wave", "Noise"}) &&
        master.info().stereoOutput && !master.hasTaps(), "GBS channel names/capabilities mismatch.");
    require(master.info().songs == 2 && master.info().playlist[1].title == "Track 02" && master.info().durationMs < 0,
        "GBS subsongs or unknown duration incorrect.");
    Block pcm{}, reference{};
    std::array<Block, 4> solos;
    for (int song = 0; song < 2; ++song) {
        for (int voice = 0; voice < 4; ++voice) {
            GmeTrack solo(path, song); solo.dry(); solo.mute(~(1u << voice));
            solo.seek(SampleRate / 2); solo.render(pcm.data(), BlockFrames);
            const int activeSide = (voice & 1) ^ song;
            require(sidePeak(pcm, activeSide) > 100 && sidePeak(pcm, 1 - activeSide) <= 1,
                "GBS solo voice missing or leaking into another stereo side.");
            if (!song) solos[voice] = pcm;
            solo.seek(3 * SampleRate); solo.seek(SampleRate / 2); solo.render(reference.data(), BlockFrames);
            require(pcm == reference, "GBS rewind changed solo waveform/routing.");
        }
    }
    require(solos[0] != solos[2] && solos[1] != solos[3], "GBS scopes duplicate different voices.");
    master.render(pcm.data(), BlockFrames);
    require(sidePeak(pcm, 0) > 100 && sidePeak(pcm, 1) > 100, "GBS master is missing stereo audio.");
    master.mute(15); for (int i = 0; i < 50; ++i) master.render(pcm.data(), BlockFrames);
    require(sidePeak(pcm, 0) <= 1 && sidePeak(pcm, 1) <= 1, "GBS all-channel mute failed.");
    master.mute(0); for (int i = 0; i < 8; ++i) master.render(pcm.data(), BlockFrames);
    require(sidePeak(pcm, 0) > 100 && sidePeak(pcm, 1) > 100, "GBS unmute failed.");
    log << "GBS: four independent voices, two subsongs, L/R routing, mute/unmute and deterministic solo rewind PASS\n"; log.flush();

    PlayerWindow window; window.player().setVolume(0); window.show(); window.loadFile(path);
    auto ready = [&] {
        const auto state = window.player().state(); const auto frame = window.player().scopes();
        return state.valid && !state.busy && frame.generation == state.generation && frame.mask == 15 &&
            std::abs(state.positionMs - frame.positionMs) < 200;
    };
    until(app, ready, "GBS player/scopes failed to load."); window.refresh();
    auto &scopes = window.scopeWindow();
    auto stereo = scopes.findChild<QCheckBox *>("scopeStereo");
    require(stereo->isEnabled(), "GBS stereo control disabled."); stereo->setChecked(true);
    require(window.findChild<QListWidget *>("songs")->count() == 2 && !window.findChild<QPushButton *>("repeat")->isEnabled(),
        "GBS playlist or unknown-duration policy failed.");
    for (int position : {12000, 250, 2000}) {
        window.player().seek(position, false); until(app, ready, "GBS paused seek scopes stuck Preparing.");
        require(!window.player().state().playing && window.player().scopes().positionMs == position, "GBS paused seek clock wrong.");
        const auto frame = window.player().scopes();
        for (int voice = 0; voice < 4; ++voice) {
            const auto &active = voice & 1 ? frame.right[voice] : frame.left[voice];
            const auto &silent = voice & 1 ? frame.left[voice] : frame.right[voice];
            require(std::any_of(active.begin(), active.end(), [](float s) { return std::abs(s) > 100; }) &&
                std::all_of(silent.begin(), silent.end(), [](float s) { return std::abs(s) <= 1; }),
                "GBS scope samples lost isolation/routing after seek.");
        }
    }
    window.player().setMuteMask(4); window.refresh(); window.findChild<QPushButton *>("next")->click();
    until(app, [&] { return ready() && window.player().state().info.song == 1; }, "GBS next subsong failed.");
    require(window.player().state().muteMask == 4, "GBS subsong change lost mutes.");
    window.refresh(); window.findChild<QPushButton *>("primary")->click();
    until(app, [&] { return !window.player().state().busy && !window.player().state().playing; }, "GBS pause failed.");
    window.findChild<QPushButton *>("primary")->click();
    until(app, [&] { return ready() && window.player().state().playing; }, "GBS resume failed.");
    window.refresh(); scopes.resize(940, 650); scopes.grab().save(QDir(directory).filePath("native-gbs-stereo.png"));
    window.grab().save(QDir(directory).filePath("native-gbs-player.png"));
    window.findChild<QPushButton *>("stop")->click();
    until(app, [&] { return !window.player().state().busy && !window.player().state().playing && window.player().state().positionMs == 0; },
        "GBS Stop did not reset transport.");
    const auto state = window.player().state();
    require(state.error.isEmpty() && state.scopeError.isEmpty() && !state.starvations && !state.outputErrors, "GBS audio/scope errors.");
    window.close();
    log << "GBS UI: four stereo scopes, playlist, paused forward/backward seeks, mute retention, next, pause/resume/stop; audio errors=0 PASS\n"; log.flush();
}

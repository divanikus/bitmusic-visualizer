#include "window.h"
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
float peak(const QVector<float>& row) { float value = 0; for (float sample : row) value = std::max(value, std::abs(sample)); return value; }
void advance(GmeTrack& track, qint64 target, QCryptographicHash* hash = nullptr) {
    std::array<short, BlockFrames * 2> output;
    while (track.frame() < target) {
        int count = int(std::min<qint64>(BlockFrames, target - track.frame()));
        track.render(output.data(), count);
        if (hash) hash->addData(QByteArrayView(reinterpret_cast<const char*>(output.data()), count * 2 * sizeof(short)));
    }
}
ScopeFrame snapshot(const GmeTrack& track) {
    ScopeFrame frame; frame.generation = 1; frame.positionMs = int(track.frame() * 1000 / SampleRate);
    int count = track.info().voices.size();
    frame.channels.resize(count); frame.left.resize(count); frame.right.resize(count);
    for (int ch = 0; ch < count; ++ch) {
        require(track.readTap(ch, frame.left[ch], frame.right[ch]), "Tap samples do not cover playback position.");
        frame.channels[ch].resize(ScopeFrames);
        for (int i = 0; i < ScopeFrames; ++i) frame.channels[ch][i] = (frame.left[ch][i] + frame.right[ch][i]) * .5f;
        frame.mask |= uint32_t(1) << ch;
    }
    return frame;
}
void checkSpcContinuity(const QString &path, QTextStream &log) {
    GmeTrack stream(path, 0, false, true);
    // The old clock mapping lost all eight channels at 22 s, 44 s, etc.
    // End-only checks at six seconds/ten minutes missed those buffer edges.
    for (int ms = 25; ms <= 120000; ms += 25) {
        advance(stream, qint64(ms) * SampleRate / 1000);
        QVector<float> left, right;
        for (int ch = 0; ch < 8; ++ch) {
            if (!stream.readTap(ch, left, right)) {
                log << "SPC tap unavailable during continuous playback at " << ms << " ms, channel " << ch << '\n'; log.flush();
                throw std::runtime_error("Continuous SPC tap history has a readiness gap.");
            }
        }
    }
    // Irregular render sizes around a formerly failing endpoint must yield
    // the same channel samples as continuous decoding, including after reset.
    GmeTrack reference(path, 0, false, true); advance(reference, 22LL * SampleRate);
    const auto expected = snapshot(reference);
    require(stream.seek(22LL * SampleRate - 64), "SPC rewind failed.");
    std::array<short, 128> tail;
    for (int count : {1, 7, 13, 43}) stream.render(tail.data(), count);
    const auto replay = snapshot(stream);
    require(replay.left == expected.left && replay.right == expected.right, "SPC buffer-edge rewind changed channel samples.");
    log << QFileInfo(path).fileName() << ": 4800 continuous SPC snapshots / all eight voices, buffer-edge rewind PASS\n";
    log.flush();
}
void write(const QString& path, const QByteArray& data) { QFile f(path); require(f.open(QIODevice::WriteOnly), "Cannot write synthetic fixture."); require(f.write(data) == data.size(), "Fixture write failed."); }
QByteArray read(const QString& path) { QFile f(path); require(f.open(QIODevice::ReadOnly), "Cannot read fixture."); return f.readAll(); }
QByteArray opllFixture(bool rhythm, int drums = 0x1f, bool switchModes = false) {
    QByteArray data(0x100, '\0'); data.replace(0, 4, "Vgm ");
    auto word = [&](int offset, quint32 value) { qToLittleEndian(value, data.data() + offset); };
    word(8, 0x150); word(12, 3579545); word(16, 3579545); word(0x34, 0xcc);
    QByteArray commands;
    auto fm = [&](int reg, int value) { commands += char(0x51); commands += char(reg); commands += char(value); };
    fm(0x30, 0x10); fm(0x10, 0x80); fm(0x20, 0x15);
    fm(0x38, 0x10); fm(0x18, 0x50); fm(0x28, rhythm && drums != 0x1f ? 0x07 : 0x17);
    for (int value : {0x80, 0x10, 0x90}) { commands += char(0x50); commands += char(value); }
    if (rhythm) {
        fm(0x36, 0); fm(0x37, 0); fm(0x38, 0);
        fm(0x16, 0x80); fm(0x26, 0x05); fm(0x17, 0x80); fm(0x27, 0x05);
        fm(0x0e, 0x20 | drums);
    }
    const int loop = data.size() + commands.size();
    for (int i = 0; i < 120; ++i) {
        const bool percussion = rhythm && (!switchModes || (i / 30) % 2 == 0);
        if (rhythm && i % 6 == 0 && percussion) { fm(0x0e, 0x20); fm(0x0e, 0x20 | drums); }
        if (switchModes && i % 30 == 0 && !percussion) {
            fm(0x0e, 0); fm(0x38, 0x10); fm(0x28, 0x07); fm(0x28, 0x17);
        }
        commands += char(0x62);
    }
    commands += char(0x66); data += commands;
    word(4, data.size() - 4); word(0x1c, loop - 0x1c); word(0x20, 88200);
    return data;
}
}

int runTapChecks(QApplication& app, const QStringList& arguments) {
    app.setQuitOnLastWindowClosed(false);
    const int option = arguments.indexOf("--tap-checks");
    const bool decoderOnly = arguments.contains("--decoder-only");
    if (option + 1 >= arguments.size()) return 2;
    QDir directory(arguments[option + 1]);
    QFile report(directory.filePath("tap-checks.txt")); if (!report.open(QIODevice::WriteOnly | QIODevice::Text)) return 3;
    QTextStream log(&report);
    try {
        QTemporaryDir fixtures(directory.filePath("tap-fixtures-XXXXXX")); require(fixtures.isValid(), "No fixture directory.");
        app.setProperty("settingsFileForTests", fixtures.filePath("BitMusicVisualizer.ini"));
        app.setProperty("themesDirectoryForTests", fixtures.filePath("themes"));
        const auto melodic = fixtures.filePath("opll-melodic.vgm"), rhythm = fixtures.filePath("opll-rhythm.vgm");
        write(melodic, opllFixture(false)); write(rhythm, opllFixture(true));
        auto leftVgm = read(directory.filePath("demo.vgm"));
        int pan = leftVgm.indexOf(QByteArray::fromHex("52b4c0")); require(pan >= 0, "FM pan fixture command not found.");
        leftVgm[pan + 2] = char(0x80);
        const auto stereoVgm = fixtures.filePath("left.vgm"); write(stereoVgm, leftVgm);
        auto leftSpc = read(directory.filePath("demo.spc")); leftSpc[0x10101] = 0;
        const auto stereoSpc = fixtures.filePath("left.spc"); write(stereoSpc, leftSpc);
        auto pcmData = leftVgm.left(0x40); QByteArray pcmCommands = QByteArray::fromHex("522b80");
        for (int i = 0; i < 4410; ++i) {
            pcmCommands += QByteArray::fromHex(i % 2 ? "522a30" : "522ad0");
            pcmCommands += QByteArray::fromHex("614000");
        }
        pcmCommands += char(0x66); pcmData += pcmCommands;
        qToLittleEndian<quint32>(pcmData.size() - 4, pcmData.data() + 4);
        qToLittleEndian<quint32>(0x40 - 0x1c, pcmData.data() + 0x1c);
        qToLittleEndian<quint32>(4410 * 64, pcmData.data() + 0x18);
        qToLittleEndian<quint32>(4410 * 64, pcmData.data() + 0x20);
        const auto pcmPath = fixtures.filePath("pcm.vgm"); write(pcmPath, pcmData);
        QStringList paths{directory.filePath("demo.vgm"), directory.filePath("demo.spc"), melodic, rhythm, stereoVgm, stereoSpc, pcmPath};
        for (int i = option + 2; i < arguments.size(); ++i) if (!arguments[i].startsWith('-')) paths << arguments[i];
        for (const auto &path : paths) if (read(path).startsWith("SNES-SPC700")) checkSpcContinuity(path, log);
        for (const auto& path : paths) {
            GmeTrack master(path), tapped(path, 0, true, true);
            require(tapped.hasTaps(), "Selected format has no shared tap implementation.");
            QCryptographicHash normalHash(QCryptographicHash::Sha256), tapHash(QCryptographicHash::Sha256);
            QElapsedTimer timer; timer.start(); advance(master, 6 * SampleRate, &normalHash); auto masterUs = timer.nsecsElapsed() / 1000;
            timer.restart(); advance(tapped, 6 * SampleRate, &tapHash); auto tapUs = timer.nsecsElapsed() / 1000;
            require(normalHash.result() == tapHash.result(), "Capturing voices changed master PCM.");
            auto frame = snapshot(tapped);
            log << "Tap peaks " << QFileInfo(path).fileName() << ": "; for (const auto& row : frame.left) log << peak(row) << ' '; log << '\n'; log.flush();
            if (path == paths[0] || path == paths[1] || path == stereoVgm || path == stereoSpc) {
                require(std::max(peak(frame.left[0]), peak(frame.right[0])) > 100, "Active voice tap silent.");
                for (int ch = 1; ch < frame.channels.size(); ++ch) {
                    const bool psgTone = ch == 7 && (path == paths[0] || path == stereoVgm);
                    require((std::max(peak(frame.left[ch]), peak(frame.right[ch])) > 1) == psgTone, "Active/silent voice tap mapping incorrect.");
                }
            }
            if (path == stereoVgm || path == stereoSpc) require(peak(frame.left[0]) > 100 && peak(frame.right[0]) <= 1, "Hard-left routing lost by tap capture.");
            if (path == pcmPath) {
                require(peak(frame.left[6]) > 100 && frame.left[6] == frame.right[6], "VGM DAC tap missing or inconsistent with mono backend routing.");
                for (int ch = 0; ch < 8; ++ch) if (ch != 6) require(peak(frame.left[ch]) <= 1, "DAC leaked into another tap.");
            }
            if (path == melodic) {
                require(tapped.info().voices.size() == 10 && tapped.info().voices[6] == "FM 7 / Bass drum" &&
                    tapped.info().voices[7] == "FM 8 / Hi-hat + Snare" && tapped.info().voices[8] == "FM 9 / Tom + Cymbal" &&
                    tapped.info().voices[9] == "PSG", "OPLL hardware groups/labels incorrect.");
                for (int ch = 0; ch < 10; ++ch) require((peak(frame.left[ch]) > 1) == (ch == 0 || ch == 8 || ch == 9), "OPLL melodic/PSG isolation failed.");
            }
            if (path == rhythm) {
                GmeTrack hits(path, 0, true, true); std::array<float, 3> drumPeaks{};
                for (int block = 1; block <= 30; ++block) {
                    advance(hits, block * BlockFrames); auto hit = snapshot(hits);
                    for (int ch = 6; ch < 9; ++ch) drumPeaks[ch - 6] = std::max(drumPeaks[ch - 6], peak(hit.left[ch]));
                }
                log << "Rhythm hardware-group onset peaks: "; for (float value : drumPeaks) log << value << ' '; log << '\n'; log.flush();
                for (float value : drumPeaks) require(value > 1, "OPLL drum tap missing.");
            }
            auto expectedLeft = frame.left;
            tapped.seek(SampleRate / 4); advance(tapped, 6 * SampleRate);
            require(snapshot(tapped).left == expectedLeft, "Tap rewind changed voice signals or timeline.");
            double solosUs = 0;
            for (int ch = 0; ch < master.info().voices.size(); ++ch) {
                GmeTrack solo(path); solo.dry(); solo.mute(~(uint32_t(1) << ch));
                timer.restart(); advance(solo, 6 * SampleRate); solosUs += timer.nsecsElapsed() / 1000.0;
            }
            log << QFileInfo(path).suffix() << ": voices=" << master.info().voices.size() << ", 6s master=" << masterUs / 1000.0
                << "ms, shared=" << tapUs / 1000.0 << "ms, solos=" << solosUs / 1000.0 << "ms, ratio=" << solosUs / std::max<qint64>(1, tapUs)
                << ", master sha256=" << normalHash.result().toHex() << " PASS\n"; log.flush();
            if (path == melodic || path == rhythm) {
                // Golden hashes from the 0.3.1 unmuted master: grouping must not change the music.
                const auto expectedHash = path == melodic ? "83281edf6e84ba2a2a5f3f05b1209f2014feab90fe0da201c6ca75111c0be527"
                    : "fa54af48a0b6fa6ced43afbba53952348525c7ef825ed784d0d93a6424cdfcaa";
                require(normalHash.result().toHex() == expectedHash, "Grouping YM2413 voices changed the unmuted master.");
                for (int ch = 0; ch < 10; ++ch) {
                    GmeTrack solo(path); solo.mute(~(uint32_t(1) << ch));
                    int soloPeak = 0; std::array<short, BlockFrames * 2> block;
                    for (int i = 0; i < 8; ++i) {
                        solo.render(block.data(), BlockFrames);
                        for (short sample : block) soloPeak = std::max(soloPeak, std::abs(int(sample)));
                    }
                    const bool sounding = ch == 0 || ch == 9 || (path == melodic ? ch == 8 : (ch >= 6 && ch < 9));
                    require((soloPeak > 1) == sounding, "YM2413 solo mask does not match its channel label.");
                }
                GmeTrack muted(path); muted.mute(~0u); std::array<short, BlockFrames * 2> block;
                advance(muted, SampleRate); muted.render(block.data(), BlockFrames);
                for (short sample : block) require(std::abs(int(sample)) <= 1, "OPLL all-mute failed.");
                muted.seek(0); advance(muted, SampleRate); muted.render(block.data(), BlockFrames);
                for (short sample : block) require(std::abs(int(sample)) <= 1, "OPLL rewind lost mute state.");
            }
            if (path == rhythm) {
                ScopeWindow scopes; PlayerState state; state.valid = true; state.generation = 1; state.info = tapped.info();
                scopes.show(); scopes.present(state, frame); app.processEvents();
                require(scopes.visibleChannels().size() == 10, "OPLL grid still exposes separate percussion cards.");
                scopes.grab().save(directory.filePath("native-opll-grouped.png")); scopes.close();
                if (!decoderOnly) {
                PlayerWindow player; player.player().setVolume(0); player.show(); player.loadFile(path);
                QElapsedTimer wait; wait.start();
                while (player.player().state().busy && wait.elapsed() < 10000) { app.processEvents(); QThread::msleep(4); }
                require(player.player().state().valid && !player.player().state().busy, "OPLL grouped player failed to load.");
                player.refresh(); player.findChild<QTabWidget*>("tabs")->setCurrentIndex(1);
                player.resize(430, 500); app.processEvents();
                player.grab().save(directory.filePath("native-opll-channels.png"));
                player.resize(380, 330); app.processEvents();
                require(player.findChild<QToolButton*>("voice9") && !player.findChild<QToolButton*>("voice10"), "OPLL mute buttons do not match the ten-card grid.");
                player.grab().save(directory.filePath("native-opll-channels-small.png")); player.close();
                }
            }
            if (path == stereoVgm) {
                ScopeWindow scopes; PlayerState state; state.valid = true; state.generation = 1; state.info = tapped.info();
                scopes.present(state, frame); scopes.show(); scopes.present(state, frame); scopes.findChild<QCheckBox*>("scopeStereo")->setChecked(true);
                scopes.findChild<QPushButton*>("editLayout")->setChecked(true); app.processEvents();
                scopes.grab().save(directory.filePath("native-stereo.png"));
                scopes.resize(480, 360); app.processEvents(); scopes.grab().save(directory.filePath("native-stereo-small.png")); scopes.close();
            }
        }
        // Each of the five percussion sounds belongs to its actual FM hardware group.
        const int drumBits[] = {0x10, 0x01, 0x08, 0x04, 0x02};
        const int groups[] = {6, 7, 7, 8, 8};
        for (int drum = 0; drum < 5; ++drum) {
            const auto path = fixtures.filePath(QString("drum-%1.vgm").arg(drum)); write(path, opllFixture(true, drumBits[drum]));
            GmeTrack hit(path, 0, true, true); std::array<float, 3> peaks{};
            for (int i = 1; i <= 8; ++i) {
                advance(hit, i * BlockFrames); const auto f = snapshot(hit);
                for (int group = 6; group < 9; ++group) peaks[group - 6] = std::max(peaks[group - 6], peak(f.left[group]));
            }
            log << "Isolated drum " << drum << " group peaks: "; for (float value : peaks) log << value << ' '; log << '\n'; log.flush();
            for (int group = 6; group < 9; ++group) require((peaks[group - 6] > 1) == (group == groups[drum]), "Percussion assigned to wrong FM group.");
            GmeTrack muted(path); // Enable every rhythm group except the one that contains this hit.
            muted.mute(~uint32_t(0x1c0) | (1u << groups[drum]));
            std::array<short, BlockFrames * 2> block;
            for (int i = 0; i < 8; ++i) { muted.render(block.data(), BlockFrames); for (short v : block) require(std::abs(int(v)) <= 1, "Hardware-group mute leaked a percussion sound."); }
        }
        const auto switching = fixtures.filePath("opll-modes.vgm"); write(switching, opllFixture(true, 0x1f, true));
        GmeTrack modes(switching, 0, true, true), soloModes(switching); soloModes.mute(~(1u << 8));
        const auto modeNames = modes.info().voices;
        for (int ms : {400, 900, 1400, 1900}) {
            advance(modes, qint64(ms) * SampleRate / 1000); auto f = snapshot(modes);
            require(f.channels.size() == 10 && modes.info().voices == modeNames, "Rhythm-mode switch changed grid identities.");
            if (ms == 900 || ms == 1900) {
                require(peak(f.left[8]) > 1, "Channel 9 lost its melodic signal after rhythm mode.");
                require(peak(f.left[6]) <= 1 && peak(f.left[7]) <= 1, "Leaving rhythm mode retained stale drums.");
            } // Percussion can decay to silence between hits; onsets are checked above.
            advance(soloModes, qint64(ms) * SampleRate / 1000);
            std::array<short, BlockFrames * 2> soloBlock; soloModes.render(soloBlock.data(), BlockFrames);
            if (ms == 900 || ms == 1900)
                require(std::any_of(soloBlock.begin(), soloBlock.end(), [](short sample) { return std::abs(int(sample)) > 1; }), "Channel 9 solo lost audio across rhythm-mode switch.");
        }
        log << "YM2413 grouping: unchanged master PCM, all five drums in FM 7-9, grouped mute, stable mode transitions PASS\n";
        GmeTrack longSpc(directory.filePath("demo.spc"), 0, false, true);
        advance(longSpc, 600LL * SampleRate);
        auto late = snapshot(longSpc);
        require(peak(late.left[0]) > 100 && peak(late.left[1]) <= 1, "Long SPC capture drifted out of its bounded history.");
        log << "Ten-minute SPC tap history/clock PASS\n";
        if (decoderOnly) log << "Decoder-only run: skipped the physical audio/player-window check.\n";
        log << "Tap checks PASS: master PCM equality, active/silent voices, stereo routing, deterministic rewind, YM2413 melodic/drum/PSG signals and mute.\n";
        return 0;
    } catch (const std::exception& e) { log << "FAIL: " << e.what() << '\n'; return 1; }
}

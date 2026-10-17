#include "miditrack.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Missing MIDI setup fixture.");
    return file.readAll();
}
QByteArray render(MusicTrack &track, int frames) {
    QByteArray output(frames * 4, 0);
    for (int done = 0; done < frames;) {
        const int count = std::min(BlockFrames, frames - done);
        track.render(reinterpret_cast<short *>(output.data()) + done * 2, count);
        done += count;
    }
    return output;
}
}

void checkMidiPatternSetup(const QString &directory, QTextStream &log) {
    const QDir folder(directory);
    const auto bytes = read(folder.filePath("setup-patterns.mid"));
    const auto songs = MidiSequence::parseSongs(bytes, SampleRate);
    require(songs.size() == 3 && songs[0].title == "A" && songs[1].title == "B" && songs[2].title == "C",
        "Note-free setup appeared in the playlist or changed pattern titles.");
    require(songs[0].endFrame == SampleRate * 3 / 4 && songs[1].endFrame == 4 * SampleRate &&
        songs[2].endFrame == SampleRate / 4, "Setup tempo, local overrides or forward-only setup scope changed.");
    for (const auto &song : songs) require(song.noteChannels == 1 && !song.mt32,
        "Hidden setup or another musical pattern changed channel usage/engine selection.");
    const auto reference = MidiSequence::parse(read(folder.filePath("setup-reference.mid")), SampleRate);
    require(songs[1].events.size() == reference.events.size(), "Setup retained note releases or lost messages.");
    for (qsizetype i = 0; i < reference.events.size(); ++i)
        require(songs[1].events[i].frame == reference.events[i].frame &&
            songs[1].events[i].message == reference.events[i].message,
            "Setup message order/timing differs from explicit initialization.");
    const auto late = MidiSequence::parse(read(folder.filePath("setup-late-notes.mid")), SampleRate);
    require(late.noteChannels == 1 && late.events[0].frame == 2 * SampleRate && late.endFrame == 3 * SampleRate,
        "Late/quiet/key-zero notes were hidden or their leading rest was removed.");
    require(MidiSequence::parse(read(folder.filePath("setup-smpte.mid")), SampleRate).endFrame == 21168,
        "Setup tempo changed SMPTE timing.");
    const auto mt = MidiSequence::parseSongs(read(folder.filePath("setup-mt32.mid")), SampleRate);
    require(mt.size() == 2 && !mt[0].mt32 && mt[1].mt32 && mt[1].endFrame == 4 * SampleRate && mt[1].noteChannels == 2,
        "Setup SysEx did not select MT-32 only for following music.");
    require(MidiSequence::parse(read(folder.filePath("setup-unaddressed.mid")), SampleRate).mt32,
        "Unaddressed setup SysEx did not follow the musical track's port.");
    const auto rejects = [](const QByteArray &data, const char *message) {
        try { MidiSequence::parseSongs(data, SampleRate); }
        catch (const std::exception &error) { return QByteArray(error.what()).contains(message); }
        return false;
    };
    require(rejects(read(folder.filePath("setup-only.mid")), "contains no notes"),
        "A setup-only SMF 2 did not explain the missing music.");
    require(rejects(read(folder.filePath("setup-port-conflict.mid")), "Multiple MIDI ports"),
        "Setup messages silently crossed MIDI ports.");
    require(rejects(bytes.left(bytes.size() - 1), "Truncated"), "Malformed trailing setup was ignored.");

    // Start B directly, without playing A or the hidden setup first.
    auto actual = openMusicTrack(folder.filePath("setup-patterns.mid"), 1, true, true);
    auto expected = openMusicTrack(folder.filePath("setup-reference.mid"), 0, true, true);
    require(actual->info().songs == 3 && actual->info().song == 1 && actual->info().playlist.size() == 3 &&
        actual->info().durationMs == 6000 && actual->info().playlist[0].durationMs == 2750 &&
        actual->info().playlist[2].durationMs == 2250, "Hidden setup broke selectable song indices or durations.");
    const auto roll = actual->info().midiResources->noteRoll;
    const auto firstRoll = midiNoteRoll(songs[0]);
    require(roll && roll->notes.size() == 1 && roll->notes[0].instrument == 80 &&
        roll->notes[0].start == 0 && roll->notes[0].end == 4 * SampleRate && roll->notes[0].key == 72 &&
        firstRoll->notes.size() == 1 && firstRoll->notes[0].instrument == 0,
        "Piano roll lost inherited instruments, local overrides or note timing.");
    const auto sound = render(*actual, SampleRate / 2);
    require(sound != QByteArray(sound.size(), 0) && sound == render(*expected, SampleRate / 2),
        "Direct subsong playback differs from explicit setup.");
    require(actual->instrumentNames(actual->frame()).value(0) == "Lead" &&
        std::abs(actual->notePitches(0).value(0) - 440 * std::pow(2., 3.5 / 12)) < .1,
        "Setup lost instrument or pitch bend.");
    auto capture = openMusicTrack("missing.mid", 1, false, true, {}, {}, actual->info().midiResources);
    require(render(*capture, SampleRate / 2) == sound, "Capture snapshot lost hidden setup.");
    require(actual->seek(0) && render(*actual, SampleRate / 2) == sound, "Rewind lost setup state.");
    require(actual->seek(3 * SampleRate) && !actual->notePitches(0).isEmpty() &&
        actual->instrumentNames(3 * SampleRate).value(0) == "Lead", "Cold seek lost setup state.");
    QVector<float> left, right;
    actual->readTap(0, left, right);
    float peakLeft = 0, peakRight = 0;
    for (float value : left) peakLeft = std::max(peakLeft, std::abs(value));
    for (float value : right) peakRight = std::max(peakRight, std::abs(value));
    require(peakLeft > 20 && peakRight < peakLeft * .01f, "Setup pan did not survive cold seek.");
    log << "SMF 2 setup: hidden rows, tempo/overrides, controls/SysEx, direct subsong, capture, rewind and cold seek PASS\n";
}

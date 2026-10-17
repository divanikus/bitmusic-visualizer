#include "miditrack.h"
#include "mt32track.h"
#include "midimetadata.h"
#include <QTextStream>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void checkIdentity(const TrackInfo &info, bool mt32) {
    const auto voices = QString(mt32 ? "Part 1|Part 2|Part 3|Part 4|Part 5|Part 6|Part 7|Part 8|Rhythm" :
        "Channel 1|Channel 2|Channel 3|Channel 4|Channel 5|Channel 6|Channel 7|Channel 8|Channel 9|Channel 10 / Drums|"
        "Channel 11|Channel 12|Channel 13|Channel 14|Channel 15|Channel 16").split('|');
    const auto sources = QString(mt32 ?
        "mt32.part:0|mt32.part:1|mt32.part:2|mt32.part:3|mt32.part:4|mt32.part:5|mt32.part:6|mt32.part:7|mt32.part:8" :
        "midi.channel:0|midi.channel:1|midi.channel:2|midi.channel:3|midi.channel:4|midi.channel:5|midi.channel:6|midi.channel:7|"
        "midi.channel:8|midi.channel:9|midi.channel:10|midi.channel:11|midi.channel:12|midi.channel:13|midi.channel:14|midi.channel:15").split('|');
    const auto id = mt32 ? "94fcc64788179a29f057a125a5ff0316db2bcd093d51e6fab6231e2d27080741" :
        "481078757e373d297c771e376f9f58ebedba352a43e11b9ceef35c1f79714091";
    require(info.voices == voices && info.configuration.sources == sources && info.configuration.id == id &&
            info.configuration.family == (mt32 ? "midi-mt32" : "midi-gm") &&
            info.configuration.name == (mt32 ? "LA32" : "FluidSynth"), "MIDI fixed identity or ordered voice/source roles changed.");
}
void checkCommonInfo(const TrackInfo &info, bool named, bool mt32) {
    require(info.path == QString::fromUtf8("Snapshot 雪.part.mid") && info.songs == 1 && info.song == 0 &&
            info.title == (named ? QString::fromUtf8("  Label 雪  ") : QString::fromUtf8("Snapshot 雪.part")) &&
            info.durationMs == (named ? 3000 : 2000) && info.stereoOutput &&
            info.tonalMask == (mt32 ? VoiceMask(255) : VoiceMask(0xffffu & ~(1u << 9))),
            "MIDI title fallback/trimming, tail rounding or capability metadata changed.");
    require(info.playlist.size() == (named ? 1 : 0), "MIDI selected-only metadata changed.");
    if (named) require(info.playlist[0].title == info.title && info.playlist[0].durationMs == info.durationMs &&
                       info.playlist[0].loopStartMs == 0, "MIDI playlist metadata changed.");
    checkIdentity(info, mt32);
}
void checkNameHistory() {
    MidiInstrumentHistory names(100);
    require(names.at(0).isEmpty(), "An empty MIDI name history has labels.");
    names.remember(0, {"A"}); names.remember(100, {"B"}); names.remember(200, {"C"});
    require(names.at(-1).isEmpty() && names.at(99) == QStringList{"A"} && names.at(100) == QStringList{"B"} &&
            names.at(200) == QStringList{"C"}, "MIDI name history changed its strict cutoff or boundary lookup.");
    names.remember(201, {"C"}); // Unchanged labels still prune old states.
    require(names.at(99).isEmpty() && names.at(100) == QStringList{"B"}, "MIDI unchanged labels stopped pruning.");
    names.remember(201, {"D"}); names.remember(201, {"E"});
    require(names.at(200) == QStringList{"C"} && names.at(201) == QStringList{"E"},
            "MIDI equal-frame labels no longer prefer the last state.");
    auto copy = names.at(201); copy[0] = "Changed copy";
    require(names.at(201) == QStringList{"E"}, "Published MIDI names share mutable storage.");
    names.clear(); require(names.at(1000).isEmpty(), "MIDI rewind retained old names.");
}
}
void checkMidiSubsongErrors() {
    checkNameHistory();
    for (bool mt32 : {false,true}) {
        try {
            if (mt32) { Mt32Track invalid("missing.mid", {}, 1, true, false); }
            else { MidiTrack invalid("missing.mid", {}, 1, true, false); }
        } catch (const std::exception &error) {
            require(QByteArray(error.what()) == "MIDI contains one song; its tracks play together.",
                    "MIDI subsong error lost precedence over data/resources.");
            continue;
        }
        throw std::runtime_error("MIDI invalid subsong accepted.");
    }
}
void checkGmMetadata(const std::shared_ptr<const MidiResources> &original, QTextStream &log) {
    require(bool(original), "GM metadata resource snapshot missing.");
    for (bool named : {false,true}) {
        auto resources = std::make_shared<MidiResources>(*original);
        resources->sequence.title = named ? QString::fromUtf8("  Label 雪  ") : QString();
        resources->sequence.endFrame = named ? SampleRate + 7 : 1;
        resources->sequence.events.clear(); resources->sequence.noteChannels = (1u << 0) | (1u << 9) | (1u << 15);
        MidiTrack track(QString::fromUtf8("Snapshot 雪.part.mid"), "ignored data", 0, named, false, resources);
        const auto &info = track.info(); checkCommonInfo(info, named, false);
        require(info.midiResources == resources && !info.mt32Resources && info.noteRoll == original->noteRoll &&
                info.system == "General MIDI" && info.emptyVoices == (VoiceMask::first(16) & ~VoiceMask(resources->sequence.noteChannels)),
                "GM metadata lost its snapshot, roll or empty-channel mask.");
    }
    log << "GM metadata: fixed identity/roles, Unicode fallback, raw title, sample-rounded tail, selected/full playlist and snapshot ownership PASS\n";
    log.flush();
}
void checkMt32Metadata(const std::shared_ptr<const Mt32Resources> &original, QTextStream &log) {
    require(bool(original), "MT-32 metadata resource snapshot missing.");
    for (bool named : {false,true}) {
        auto resources = std::make_shared<Mt32Resources>(*original);
        resources->sequence.title = named ? QString::fromUtf8("  Label 雪  ") : QString();
        resources->sequence.endFrame = named ? SampleRate + 7 : 1;
        resources->sequence.events.clear(); resources->model = "  Original model  ";
        Mt32Track track(QString::fromUtf8("Snapshot 雪.part.mid"), "ignored data", 0, named, false, resources);
        const auto &info = track.info(); checkCommonInfo(info, named, true);
        require(info.mt32Resources == resources && !info.midiResources && info.noteRoll && info.noteRoll->notes.isEmpty() &&
                info.system == resources->model && info.emptyVoices == VoiceMask::first(9),
                "MT-32 metadata lost its snapshot, roll, raw model or empty-part mask.");
    }
    log << "MT-32 metadata: fixed identity/roles, Unicode fallback, raw title/model, sample-rounded tail, selected/full playlist and snapshot ownership PASS\n";
    log.flush();
}

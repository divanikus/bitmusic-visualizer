#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include "musicevents.h"
#include <functional>

// Engine-independent SMF timeline, reusable by the MT-32 backend.
struct MidiEvent { qint64 frame = 0; QByteArray message; };
struct MidiSequence {
    QVector<MidiEvent> events;
    QString title;
    qint64 endFrame = 0;
    quint16 noteChannels = 0; // Positive-velocity Note On anywhere in the file.
    bool mt32 = false;
    static MidiSequence parse(const QByteArray &bytes, int sampleRate);
};
// Key-down durations, not acoustic envelopes. Optional routing is resolved by
// the backend at each event (MT-32 SysEx can reassign MIDI channels to parts).
std::shared_ptr<const NoteRoll> midiNoteRoll(const MidiSequence &sequence,
    const std::function<VoiceMask(const MidiEvent &)> &routing = {});

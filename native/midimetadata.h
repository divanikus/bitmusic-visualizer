#pragma once
#include "midisequence.h"
#include <QStringList>
#include <deque>

QString midiTrackTitle(const MidiSequence &sequence, const QString &path);
int midiTrackDurationMs(const MidiSequence &sequence);

// Labels follow the audible frame, including the state preceding decoder read-ahead.
class MidiInstrumentHistory {
public:
    explicit MidiInstrumentHistory(qint64 keepFrames) : keepFrames_(keepFrames) {}
    void clear() { states_.clear(); }
    void remember(qint64 frame, const QStringList &names);
    QStringList at(qint64 frame) const;
private:
    struct State { qint64 frame; QStringList names; };
    qint64 keepFrames_;
    std::deque<State> states_;
};

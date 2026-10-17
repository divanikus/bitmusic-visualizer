#pragma once
#include "midisequence.h"
#include "engine.h"
#include <QStringList>
#include <deque>

QVector<SongInfo> midiSongList(const QVector<MidiSequence> &songs, const QString &path);
void midiSongInfo(TrackInfo &info, const MidiSequence &sequence, const QVector<SongInfo> &songs, int song, bool playlist);
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

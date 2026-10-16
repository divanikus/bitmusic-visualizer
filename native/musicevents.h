#pragma once
#include "voicemask.h"
#include <QVector>
#include <QString>
#include <memory>

// Immutable, decoded musical events. Never accessed by the audio callback.
struct RollNote {
    qint64 start = 0, end = 0;
    int source = 0, key = 60, instrument = 0, velocity = 0;
};
struct NoteRoll {
    QVector<RollNote> notes;
    QVector<qint64> maxEnds;
    int low = 48, high = 83;
    void index();
    QVector<int> visible(qint64 from, qint64 to, int source = -1, int limit = 4096) const;
};
struct PatternCell { quint8 note=0, instrument=0, volume=0, effect=0, parameter=0, effect2=0, parameter2=0; };
struct TrackerPattern { int rows=0; QVector<PatternCell> cells; };
struct TrackerPatterns { int channels=0; QVector<TrackerPattern> patterns; };
struct PatternPosition {
    int order=-1, pattern=-1, row=0, speed=0, bpm=0;
};
QString trackerNoteName(int note);

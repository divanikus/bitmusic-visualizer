#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

// Engine-independent SMF timeline, reusable by the MT-32 backend.
struct MidiEvent { qint64 frame = 0; QByteArray message; };
struct MidiSequence {
    QVector<MidiEvent> events;
    QString title;
    qint64 endFrame = 0;
    qint64 firstNoteFrame = -1; // First positive-velocity Note On, not a measured audio onset.
    bool mt32 = false;
    static MidiSequence parse(const QByteArray &bytes, int sampleRate);
};

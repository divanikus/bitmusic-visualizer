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
    quint16 noteChannels = 0; // Positive-velocity Note On anywhere in the file.
    bool mt32 = false;
    static MidiSequence parse(const QByteArray &bytes, int sampleRate);
};

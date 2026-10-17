#pragma once
#include "vgm.h"

constexpr quint32 YamahaSampleMemoryLimit = 1u << 24; // ADPCM address bus, per bank.
struct YamahaSampleBlock {
    int bank; // 0 = ADPCM-A, 1 = ADPCM-B.
    quint32 total, offset;
    QByteArray bytes;
};
struct YamahaStream {
    struct Event { qint64 tick; int chip, reg, value; };
    QVector<Event> events;
    QVector<YamahaSampleBlock> blocks;
    qsizetype loopEvent = 0;
    qint64 ticks = 0, loopTick = 0, loopDuration = 0;
    quint32 declaredTicks = 0; // Zero keeps unknown duration metadata even when the stream has a known end.
};
// Called after file-size/support checks with the header from these same decoded bytes.
YamahaStream readYamahaStream(const QByteArray &data, const VgmHeader &header);

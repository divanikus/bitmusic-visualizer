#include "gmevgm.h"
#include <QtEndian>

namespace {
quint32 word(const QByteArray &data, qsizetype at) { return qFromLittleEndian<quint32>(data.constData() + at); }
}
TrackBoundary gmeVgmBoundary(const QByteArray &data, qsizetype dataOffset) {
    if (data.size() < 0x40) return {};
    quint64 pos = quint64(dataOffset);
    const quint64 loop = word(data, 0x1c) ? 0x1cULL + word(data, 0x1c) : 0;
    qint64 ticks = 0, loopFrame = -1;
    while (pos < quint64(data.size())) {
        if (pos == loop) loopFrame = ticks;
        const auto op = quint8(data[qsizetype(pos++)]);
        const auto remaining = quint64(data.size()) - pos;
        if (op == 0x66)
            return ticks > 0 && (!loop || (loopFrame >= 0 && loopFrame < ticks)) ? TrackBoundary{ticks, loopFrame} : TrackBoundary{};
        if (op == 0x61) {
            if (remaining < 2) return {};
            ticks += qFromLittleEndian<quint16>(data.constData() + pos);
            pos += 2;
        } else if (op == 0x62) ticks += 735;
        else if (op == 0x63) ticks += 882;
        else if (op >= 0x70 && op <= 0x7f) ticks += (op & 15) + 1;
        else if (op >= 0x80 && op <= 0x8f) ticks += op & 15;
        else if (op == 0x4f || op == 0x50 || op == 0x3f || op == 0x30) {
            if (!remaining) return {};
            ++pos;
        } else if ((op >= 0x51 && op <= 0x53) || (op >= 0xa1 && op <= 0xa3)) {
            if (remaining < 2) return {};
            pos += 2;
        } else if (op == 0xe0) {
            if (remaining < 4) return {};
            pos += 4;
        } else if (op == 0x67) {
            if (remaining < 6 || quint8(data[qsizetype(pos)]) != 0x66) return {};
            const quint64 size = word(data, qsizetype(pos + 2)) & 0x7fffffffu;
            if (size > remaining - 6) return {};
            pos += 6 + size;
        } else return {}; // Do not guess timing for an unsupported/legacy command.
    }
    return {};
}
bool gmeVgmStereoRouting(const QByteArray &data, qsizetype dataOffset) {
    // Used only for libgme's PSG-only VGM path. FM paths have fixed capabilities.
    if (data.size() < 0x40) return false;
    if ((word(data, 12) & 0xc0000000u) == 0xc0000000u) return true; // T6W28 routing.
    quint64 pos = quint64(dataOffset);
    const quint64 end = quint64(data.size());
    // Walk command boundaries, not raw bytes: sample data/metadata can contain 0x4f.
    while (pos < end) {
        const auto command = static_cast<unsigned char>(data[qsizetype(pos)]);
        if (command == 0x66) break;
        quint64 length = 1;
        if (command == 0x67) {
            if (end - pos < 7) break;
            length = 7ULL + (word(data, qsizetype(pos + 3)) & 0x7fffffffu);
        } else if (command == 0x68) length = 12;
        else if (command == 0x50 || command == 0x64) length = 2; // 0x64 is a byte delay in this backend.
        else if (command == 0x61) length = 3;
        else if (command >= 0x90 && command <= 0x95) {
            constexpr int lengths[] = {5, 5, 6, 11, 2, 5}; length = lengths[command - 0x90];
        } else if (command >= 0x30 && command < 0x50) length = 2;
        else if ((command >= 0x51 && command <= 0x5f) || (command >= 0xa0 && command <= 0xbf)) length = 3;
        else if (command >= 0xc0 && command <= 0xdf) length = 4;
        else if (command >= 0xe0) length = 5;
        if (length > end - pos) break;
        if (command == 0x4f || command == 0x3f) {
            const auto pan = static_cast<unsigned char>(data[qsizetype(pos + 1)]);
            if ((pan & 15) != (pan >> 4)) return true;
        }
        pos += length;
    }
    return false;
}

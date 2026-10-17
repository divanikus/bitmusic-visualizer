#include "yamahastream.h"
#include <QtEndian>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
quint32 word(const QByteArray &data, qsizetype at) { return qFromLittleEndian<quint32>(data.constData() + at); }
void need(const QByteArray &data, qsizetype pos, int count) {
    require(data.size() - pos >= count, "Truncated Yamaha VGM command.");
}
int chipIndex(const QVector<VgmYamahaChip> &specs, int id, int instance) {
    const auto it = std::find_if(specs.begin(), specs.end(), [&](const auto &s) { return s.id == id && s.instance == instance; });
    require(it != specs.end(), "VGM command targets an undeclared Yamaha chip.");
    return int(it - specs.begin());
}
void readSampleBlock(const QByteArray &data, qsizetype &pos, const VgmHeader &h, YamahaStream &stream) {
    need(data, pos, 6); require(quint8(data[pos++]) == 0x66, "Invalid VGM data-block marker.");
    const auto type = quint8(data[pos++]);
    const auto rawSize = word(data, pos); pos += 4;
    const auto size = rawSize & 0x7fffffffu;
    require(size >= 8 && quint64(size) <= quint64(data.size()-pos), "Truncated VGM sample block.");
    const int id = type == 0x81 ? 7 : type == 0x82 || type == 0x83 ? 8 : type == 0x88 ? 11 : -1;
    require(id >= 0, "Unsupported Yamaha VGM data-block type.");
    const int chip = chipIndex(h.yamaha, id,int(rawSize>>31));
    const auto total = word(data, pos), offset = word(data, pos+4);
    require(total && total <= YamahaSampleMemoryLimit && offset <= total && size-8 <= total-offset,
            "Invalid Yamaha VGM sample-memory range (maximum 16 MiB per bank).");
    require(stream.blocks.size() < 65536 && stream.events.size() < 2000000, "Yamaha VGM has too many sample blocks.");
    stream.events.push_back({stream.ticks,chip,-1,int(stream.blocks.size())});
    stream.blocks.push_back({type == 0x82 ? 0 : 1,total,offset,data.mid(pos+8,size-8)});
    pos += size;
}
}

YamahaStream readYamahaStream(const QByteArray &data, const VgmHeader &h) {
    YamahaStream stream;
    const quint64 loop = word(data, 0x1c) ? 0x1cULL+word(data, 0x1c) : 0;
    bool loopFound = false, endFound = false;
    int wait62 = 735, wait63 = 882;
    std::array<int,2> loopWait{};
    std::array<bool,2> loopOverride{}, loopUsesEntryWait{};
    for (qsizetype pos = h.dataOffset; pos < data.size();) {
        if (quint64(pos) == loop) {
            stream.loopTick = stream.ticks; stream.loopEvent = stream.events.size(); loopFound = true;
            loopWait = {wait62,wait63};
        }
        const int op = quint8(data[pos++]);
        if (op == 0x66) { endFound = true; break; }
        if (op == 0x61) { need(data, pos, 2); stream.ticks += qFromLittleEndian<quint16>(data.constData()+pos); pos += 2; }
        else if (op == 0x62 || op == 0x63) {
            stream.ticks += op == 0x62 ? wait62 : wait63;
            if (loopFound && !loopOverride[op-0x62]) loopUsesEntryWait[op-0x62] = true;
        }
        else if (op >= 0x70 && op <= 0x7f) stream.ticks += (op&15)+1;
        else if (op == 0x64) {
            need(data, pos, 3); const auto target = quint8(data[pos++]);
            const int length = qFromLittleEndian<quint16>(data.constData()+pos); pos += 2;
            require(target == 0x62 || target == 0x63, "Invalid VGM wait override.");
            (target == 0x62 ? wait62 : wait63) = length;
            if (loopFound) loopOverride[target-0x62] = true;
        } else if (op == 0x67) {
            readSampleBlock(data, pos, h, stream);
        } else {
            const auto command = yamahaRegisterCommand(op);
            if (command.chipId < 0) throw std::runtime_error(QString("Unsupported command 0x%1 in Yamaha VGM.").arg(op,2,16,QLatin1Char('0')).toStdString());
            need(data, pos, 2); const int reg = quint8(data[pos++]) + command.registerBase, value = quint8(data[pos++]);
            require(stream.events.size() < 2000000, "Yamaha VGM has too many register writes.");
            stream.events.push_back({stream.ticks,chipIndex(h.yamaha, command.chipId,command.instance),reg,value});
        }
    }
    require(endFound, "Yamaha VGM has no end command.");
    require(!loop || (loopFound && stream.ticks > stream.loopTick), "Invalid or zero-duration VGM loop.");
    require(!(loopUsesEntryWait[0] && loopWait[0] != wait62) && !(loopUsesEntryWait[1] && loopWait[1] != wait63),
            "Unsupported VGM loop with changing wait lengths.");
    if (loop) stream.loopDuration = stream.ticks-stream.loopTick;
    stream.declaredTicks = word(data, 0x18);
    require(!stream.declaredTicks || stream.declaredTicks == stream.ticks, "VGM duration disagrees with its command stream.");
    return stream;
}

#include "vgm.h"
#include <QtEndian>
#include <algorithm>
#include <stdexcept>

VgmHeader inspectVgm(const QByteArray &data) {
    if (data.size() < 0x40 || !data.startsWith("Vgm ")) throw std::runtime_error("Invalid VGM header.");
    auto word = [&](int at) { return qFromLittleEndian<quint32>(data.constData() + at); };
    VgmHeader h; h.version = word(8);
    h.dataOffset = h.version >= 0x150 && word(0x34) ? 0x34ULL + word(0x34) : 0x40;
    if (h.dataOffset < 0x40 || h.dataOffset > data.size()) throw std::runtime_error("Invalid VGM data offset.");
    h.headerEnd = h.dataOffset;
    if (h.version >= 0x170 && h.headerEnd >= 0xc0 && word(0xbc)) {
        const quint64 extra = 0xbcULL + word(0xbc);
        if (extra < 0xc0 || extra >= quint64(h.dataOffset)) throw std::runtime_error("Invalid VGM extra header.");
        h.headerEnd = qsizetype(extra);
    }
    struct Chip { int offset; quint32 version; const char *name; };
    static constexpr Chip chips[] = {
        {0x0c,0x100,"SN76489"}, {0x10,0x100,"YM2413"}, {0x2c,0x110,"YM2612"},
        {0x30,0x110,"YM2151"}, {0x38,0x151,"Sega PCM"}, {0x40,0x151,"RF5C68"},
        {0x44,0x151,"YM2203"}, {0x48,0x151,"YM2608"}, {0x4c,0x151,"YM2610"},
        {0x50,0x151,"YM3812"}, {0x54,0x151,"YM3526"}, {0x58,0x151,"Y8950"},
        {0x5c,0x151,"YMF262"}, {0x60,0x151,"YMF278B"}, {0x64,0x151,"YMF271"},
        {0x68,0x151,"YMZ280B"}, {0x6c,0x151,"RF5C164"}, {0x70,0x151,"PWM"},
        {0x74,0x151,"AY8910"}, {0x80,0x161,"Game Boy APU"}, {0x84,0x161,"NES APU"},
        {0x88,0x161,"MultiPCM"}, {0x8c,0x161,"uPD7759"}, {0x90,0x161,"OKIM6258"},
        {0x98,0x161,"OKIM6295"}, {0x9c,0x161,"K051649"}, {0xa0,0x161,"K054539"},
        {0xa4,0x161,"HuC6280"}, {0xa8,0x161,"C140"}, {0xac,0x161,"K053260"},
        {0xb0,0x161,"POKEY"}, {0xb4,0x161,"QSound"}, {0xb8,0x171,"SCSP"},
        {0xc0,0x171,"WonderSwan"}, {0xc4,0x171,"VSU"}, {0xc8,0x171,"SAA1099"},
        {0xcc,0x171,"ES5503"}, {0xd0,0x171,"ES5506"}, {0xd8,0x171,"X1-010"},
        {0xdc,0x171,"C352"}, {0xe0,0x171,"GA20"}
    };
    for (const auto &chip : chips) {
        if (h.version < chip.version || chip.offset+4 > h.headerEnd || !word(chip.offset)) continue;
        const int id = chip.offset == 0x30 ? 3 : chip.offset >= 0x44 && chip.offset <= 0x5c ?
            6 + (chip.offset-0x44)/4 : -1;
        if (id >= 0) {
            const auto raw = word(chip.offset), clock = raw & 0x3fffffffu;
            const bool dual = raw & 0x40000000u, variant = raw & 0x80000000u;
            if (clock < 100000 || clock > 20000000 || (variant && !(id == 9 && dual) && id != 8))
                h.unsupported << QString::fromLatin1(chip.name) + " clock/variant";
            for (int n = 0; n < (dual ? 2 : 1); ++n)
                h.yamaha.push_back({id, n, clock, id == 8 && variant ? QString("YM2610B") : QString::fromLatin1(chip.name),
                                   id == 3 || id == 7 || id == 8 || id == 12 || variant,
                                   id == 9 && variant ? (n ? 1 : -1) : 0, 1, 1, id == 8 && variant});
        } else if (chip.offset == 0xb0) h.pokeyClock = word(chip.offset);
        else if (chip.offset == 0x0c || chip.offset == 0x10 || chip.offset == 0x2c) h.sega = true;
        else h.unsupported << QString::fromLatin1(chip.name);
    }
    if (h.pokeyClock & 0xc0000000u) h.unsupported << "dual/variant POKEY";
    if (h.pokeyClock && h.sega) h.unsupported << "POKEY combined with other chips";
    if (!h.yamaha.isEmpty() && (h.sega || h.pokeyClock)) h.unsupported << "Yamaha combined with Sega/POKEY chips";
    if (word(0x10) && word(0x2c) && h.version >= 0x110) h.unsupported << "YM2413 + YM2612 together";
    // Extended clocks and volumes are relative to their own offset fields.
    if (!h.yamaha.isEmpty() && h.headerEnd < h.dataOffset) {
        const auto at = h.headerEnd;
        auto valid = [&](bool ok) { if (!ok) throw std::runtime_error("Invalid VGM chip clock/volume header."); };
        valid(at + 4 <= h.dataOffset);
        const auto size = word(int(at));
        valid(size >= 4 && size <= quint64(h.dataOffset-at));
        for (int kind = 0; kind < 2; ++kind) {
            const int field = 4 + 4*kind;
            if (size < quint32(field+4) || !word(int(at+field))) continue;
            const quint64 table = quint64(at+field)+word(int(at+field));
            valid(table >= quint64(at)+size && table < quint64(h.dataOffset));
            const int count = quint8(data[qsizetype(table)]), stride = kind ? 4 : 5;
            valid(table+1+count*stride <= quint64(h.dataOffset));
            for (int i = 0; i < count; ++i) {
                const auto p = qsizetype(table+1+i*stride);
                const int id = quint8(data[p]);
                const int instance = kind ? quint8(data[p+1]) & 1 : 1;
                auto found = std::find_if(h.yamaha.begin(), h.yamaha.end(), [&](const auto &c) {
                    return c.id == (id & 0x7f) && c.instance == instance;
                });
                valid(found != h.yamaha.end());
                if (!kind) {
                    found->clock = word(int(p+1)); valid(found->clock >= 100000 && found->clock <= 20000000);
                } else {
                    valid(!(quint8(data[p+1]) & ~1) && (!(id & 0x80) || (found->id >= 6 && found->id <= 8)));
                    const auto volume = qFromLittleEndian<quint16>(data.constData()+p+2);
                    auto &gain = (id & 0x80) ? found->ssgGain : found->gain;
                    gain = (volume & 0x8000) ? gain * (volume & 0x7fff) / 256.0 : volume / 256.0;
                }
            }
        }
    }
    return h;
}
void requireSupportedVgm(const VgmHeader &h) {
    if (!h.unsupported.isEmpty())
        throw std::runtime_error(("Unsupported sound chip in VGM/VGZ: " + h.unsupported.join(", ") +
            ".\nThis player cannot play this chip configuration yet.").toStdString());
}

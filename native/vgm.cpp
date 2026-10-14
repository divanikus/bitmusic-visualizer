#include "vgm.h"
#include <QtEndian>
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
        if (chip.offset == 0xb0) h.pokeyClock = word(chip.offset);
        else if (chip.offset == 0x0c || chip.offset == 0x10 || chip.offset == 0x2c) h.sega = true;
        else h.unsupported << QString::fromLatin1(chip.name);
    }
    if (h.pokeyClock & 0xc0000000u) h.unsupported << "dual/variant POKEY";
    if (h.pokeyClock && h.sega) h.unsupported << "POKEY combined with other chips";
    if (word(0x10) && word(0x2c) && h.version >= 0x110) h.unsupported << "YM2413 + YM2612 together";
    return h;
}
void requireSupportedVgm(const VgmHeader &h) {
    if (!h.unsupported.isEmpty())
        throw std::runtime_error(("Unsupported sound chip in VGM/VGZ: " + h.unsupported.join(", ") +
            ".\nThis player cannot play this chip configuration yet.").toStdString());
}

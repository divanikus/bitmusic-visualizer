#include "vgm.h"
#include <QtEndian>
#include <algorithm>
#include <stdexcept>

namespace {
struct ChipDescription {
    int offset;
    quint32 version;
    const char *name;
    int yamahaId = -1, writeCommand = 0, writePorts = 0;
    bool stereo = false;
};
// Keep field order: it determines chip/source order and unsupported-message order.
constexpr ChipDescription Chips[] = {
    {0x0c,0x100,"SN76489"}, {0x10,0x100,"YM2413"}, {0x2c,0x110,"YM2612"},
    {0x30,0x110,"YM2151",3,0x54,1,true}, {0x38,0x151,"Sega PCM"}, {0x40,0x151,"RF5C68"},
    {0x44,0x151,"YM2203",6,0x55,1}, {0x48,0x151,"YM2608",7,0x56,2,true}, {0x4c,0x151,"YM2610",8,0x58,2,true},
    {0x50,0x151,"YM3812",9,0x5a,1}, {0x54,0x151,"YM3526",10,0x5b,1}, {0x58,0x151,"Y8950",11,0x5c,1},
    {0x5c,0x151,"YMF262",12,0x5e,2,true}, {0x60,0x151,"YMF278B"}, {0x64,0x151,"YMF271"},
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

quint32 word(const QByteArray &data, int at) { return qFromLittleEndian<quint32>(data.constData() + at); }
VgmHeader readHeaderBounds(const QByteArray &data) {
    if (data.size() < 0x40 || !data.startsWith("Vgm ")) throw std::runtime_error("Invalid VGM header.");
    VgmHeader h; h.version = word(data, 8);
    h.dataOffset = h.version >= 0x150 && word(data, 0x34) ? 0x34ULL + word(data, 0x34) : 0x40;
    if (h.dataOffset < 0x40 || h.dataOffset > data.size()) throw std::runtime_error("Invalid VGM data offset.");
    h.headerEnd = h.dataOffset;
    if (h.version >= 0x170 && h.headerEnd >= 0xc0 && word(data, 0xbc)) {
        const quint64 extra = 0xbcULL + word(data, 0xbc);
        if (extra < 0xc0 || extra >= quint64(h.dataOffset)) throw std::runtime_error("Invalid VGM extra header.");
        h.headerEnd = qsizetype(extra);
    }
    return h;
}
void appendYamahaChips(VgmHeader &h, const ChipDescription &chip, quint32 raw) {
    const int id = chip.yamahaId;
    const auto clock = raw & 0x3fffffffu;
    const bool dual = raw & 0x40000000u, variant = raw & 0x80000000u;
    if (clock < 100000 || clock > 20000000 || (variant && !(id == 9 && dual) && id != 8))
        h.unsupported << QString::fromLatin1(chip.name) + " clock/variant";
    for (int n = 0; n < (dual ? 2 : 1); ++n)
        h.yamaha.push_back({id, n, clock, id == 8 && variant ? QString("YM2610B") : QString::fromLatin1(chip.name),
                           chip.stereo || variant, id == 9 && variant ? (n ? 1 : -1) : 0, 1, 1, id == 8 && variant});
}
void readChipClocks(const QByteArray &data, VgmHeader &h) {
    for (const auto &chip : Chips) {
        if (h.version < chip.version || chip.offset + 4 > h.headerEnd) continue;
        const auto raw = word(data, chip.offset);
        if (!raw) continue;
        if (chip.yamahaId >= 0) appendYamahaChips(h, chip, raw);
        else if (chip.offset == 0xb0) h.pokeyClock = raw;
        else if (chip.offset == 0x0c || chip.offset == 0x10 || chip.offset == 0x2c) h.sega = true;
        else h.unsupported << QString::fromLatin1(chip.name);
    }
}
void checkChipCombinations(const QByteArray &data, VgmHeader &h) {
    if (h.pokeyClock & 0xc0000000u) h.unsupported << "dual/variant POKEY";
    if (h.pokeyClock && h.sega) h.unsupported << "POKEY combined with other chips";
    if (!h.yamaha.isEmpty() && (h.sega || h.pokeyClock)) h.unsupported << "Yamaha combined with Sega/POKEY chips";
    if (word(data, 0x10) && word(data, 0x2c) && h.version >= 0x110) h.unsupported << "YM2413 + YM2612 together";
}
void readYamahaExtraHeader(const QByteArray &data, VgmHeader &h) {
    // Extended clocks and volumes are relative to their own offset fields.
    if (!h.yamaha.isEmpty() && h.headerEnd < h.dataOffset) {
        const auto at = h.headerEnd;
        auto valid = [&](bool ok) { if (!ok) throw std::runtime_error("Invalid VGM chip clock/volume header."); };
        valid(at + 4 <= h.dataOffset);
        const auto size = word(data, int(at));
        valid(size >= 4 && size <= quint64(h.dataOffset-at));
        for (int kind = 0; kind < 2; ++kind) {
            const int field = 4 + 4*kind;
            if (size < quint32(field+4) || !word(data, int(at+field))) continue;
            const quint64 table = quint64(at+field)+word(data, int(at+field));
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
                    found->clock = word(data, int(p+1)); valid(found->clock >= 100000 && found->clock <= 20000000);
                } else {
                    valid(!(quint8(data[p+1]) & ~1) && (!(id & 0x80) || (found->id >= 6 && found->id <= 8)));
                    const auto volume = qFromLittleEndian<quint16>(data.constData()+p+2);
                    auto &gain = (id & 0x80) ? found->ssgGain : found->gain;
                    gain = (volume & 0x8000) ? gain * (volume & 0x7fff) / 256.0 : volume / 256.0;
                }
            }
        }
    }
}
}

VgmHeader inspectVgm(const QByteArray &data) {
    auto h = readHeaderBounds(data);
    readChipClocks(data, h);
    checkChipCombinations(data, h);
    readYamahaExtraHeader(data, h);
    return h;
}
void requireSupportedVgm(const VgmHeader &h) {
    if (!h.unsupported.isEmpty())
        throw std::runtime_error(("Unsupported sound chip in VGM/VGZ: " + h.unsupported.join(", ") +
            ".\nThis player cannot play this chip configuration yet.").toStdString());
}

YamahaRegisterCommand yamahaRegisterCommand(int opcode) {
    const int primary = opcode >= 0xa0 && opcode <= 0xaf ? opcode - 0x50 : opcode;
    for (const auto &chip : Chips)
        if (chip.yamahaId >= 0 && primary >= chip.writeCommand && primary < chip.writeCommand + chip.writePorts)
            return {chip.yamahaId, opcode >= 0xa0 ? 1 : 0, 256 * (primary - chip.writeCommand)};
    return {};
}

QVector<YamahaVoice> yamahaVoices(const VgmYamahaChip &chip) {
    QVector<YamahaVoice> result;
    const bool opn=chip.id==7||chip.id==8, opl=chip.id>=9;
    const int fm=chip.id==3?8:chip.id==6?3:opn?6:chip.id==12?18:9;
    for(int c=0;c<fm;++c) {
        if(chip.id==8&&!chip.variant&&(c==0||c==3)) continue;
        QString label=QString("FM %1").arg(c+1);
        if(chip.id==3&&c==7) label+=" / Noise";
        if(opl&&c==6) label+=" / Bass drum";
        if(opl&&c==7) label+=" / Hi-hat + Snare";
        if(opl&&c==8) label+=" / Tom + Cymbal";
        if(chip.id==12) {
            if(c%9<3) label+=QString(" / %1+%2 (4-op)").arg(c+1).arg(c+4);
            else if(c%9<6) label+=" (2-op only)";
        }
        result.push_back({c,QString("fm:%1").arg(c),label});
    }
    if(chip.id==6||opn) for(int c=0;c<3;++c)
        result.push_back({(opn?6:3)+c,QString("ssg:%1").arg(c),QString("PSG %1").arg(QChar('A'+c))});
    if(opn) {
        const char *drums[]={"Bass drum","Snare","Cymbal","Hi-hat","Tom","Rim shot"};
        for(int c=0;c<6;++c) result.push_back({9+c,QString("adpcm-a:%1").arg(c),
            chip.id==7?QString::fromLatin1(drums[c]):QString("ADPCM-A %1").arg(c+1)});
    }
    if(opn||chip.id==11) result.push_back({opn?15:9,"adpcm-b:0","ADPCM-B"});
    return result;
}

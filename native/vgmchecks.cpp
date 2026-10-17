#include "vgm.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QTextStream>
#include <QtEndian>
#include <stdexcept>
#include <utility>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void word(QByteArray &data, int at, quint32 value) { qToLittleEndian(value, data.data() + at); }
QByteArray vgm(quint32 version = 0x171, int start = 0x100) {
    QByteArray data(0x101, '\0'); data.replace(0, 4, "Vgm ");
    word(data, 8, version);
    if (version >= 0x150) word(data, 0x34, start - 0x34);
    return data;
}
void reject(const QByteArray &data, const char *error) {
    try { inspectVgm(data); }
    catch (const std::exception &e) { require(QByteArray(e.what()) == error, "VGM header error text changed."); return; }
    throw std::runtime_error("Malformed VGM header accepted.");
}
void supportError(const VgmHeader &header, const QStringList &errors) {
    require(header.unsupported == errors, "VGM unsupported-chip names/order changed.");
    try { requireSupportedVgm(header); }
    catch (const std::exception &e) {
        const auto expected = "Unsupported sound chip in VGM/VGZ: " + errors.join(", ") +
            ".\nThis player cannot play this chip configuration yet.";
        require(QString::fromUtf8(e.what()) == expected, "VGM unsupported-chip error text changed."); return;
    }
    require(errors.isEmpty(), "Unsupported VGM configuration accepted.");
}
void checkHeaderClocks() {
    // Format facts are explicit here so moving production descriptors cannot silently change admission.
    struct Clock { int at; quint32 version; const char *name; int yamaha = -1; };
    const Clock clocks[] = {
        {0x0c,0x100,"SN76489"}, {0x10,0x100,"YM2413"}, {0x2c,0x110,"YM2612"},
        {0x30,0x110,"YM2151",3}, {0x38,0x151,"Sega PCM"}, {0x40,0x151,"RF5C68"},
        {0x44,0x151,"YM2203",6}, {0x48,0x151,"YM2608",7}, {0x4c,0x151,"YM2610",8},
        {0x50,0x151,"YM3812",9}, {0x54,0x151,"YM3526",10}, {0x58,0x151,"Y8950",11},
        {0x5c,0x151,"YMF262",12}, {0x60,0x151,"YMF278B"}, {0x64,0x151,"YMF271"},
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
    for (const auto &clock : clocks) {
        auto data = vgm(clock.version); word(data, clock.at, 3579545);
        const auto h = inspectVgm(data);
        require(h.version == clock.version && h.dataOffset == (clock.version >= 0x150 ? 0x100 : 0x40) &&
                h.headerEnd == h.dataOffset, "VGM version/data/header bounds changed.");
        if (clock.yamaha >= 0) {
            require(h.yamaha.size() == 1, "VGM Yamaha clock routing changed.");
            const auto &chip = h.yamaha[0];
            require(chip.id == clock.yamaha && chip.name == clock.name && chip.clock == 3579545 &&
                    chip.instance == 0 && chip.pan == 0 && !chip.variant && chip.gain == 1 && chip.ssgGain == 1 &&
                    chip.stereo == (clock.yamaha == 3 || clock.yamaha == 7 || clock.yamaha == 8 || clock.yamaha == 12),
                    "VGM Yamaha description changed.");
            supportError(h, {});
        } else if (clock.at == 0xb0) require(h.pokeyClock == 3579545 && h.unsupported.isEmpty(), "POKEY clock routing changed.");
        else if (clock.at == 0x0c || clock.at == 0x10 || clock.at == 0x2c)
            require(h.sega && h.unsupported.isEmpty(), "Sega clock routing changed.");
        else supportError(h, {QString::fromLatin1(clock.name)});
        data = vgm(clock.version - 1); word(data, clock.at, 3579545);
        const auto older = inspectVgm(data);
        require(older.yamaha.isEmpty() && !older.sega && !older.pokeyClock && older.unsupported.isEmpty(),
                "A clock before its VGM version became visible.");
        if (clock.at >= 0x40) {
            data = vgm(0x171, clock.at + 3); word(data, clock.at, 3579545);
            const auto shortHeader = inspectVgm(data);
            require(shortHeader.yamaha.isEmpty() && !shortHeader.pokeyClock && shortHeader.unsupported.isEmpty(),
                    "A clock crossing the data boundary became visible.");
        }
    }
    for (quint32 rate : {99999u,100000u,20000000u,20000001u}) {
        auto data = vgm(); word(data, 0x44, rate);
        const auto h = inspectVgm(data);
        supportError(h, rate == 99999 || rate == 20000001 ? QStringList{"YM2203 clock/variant"} : QStringList{});
    }
    auto data = vgm();
    for (const auto &clock : {std::pair{0x0c,1u}, {0x10,1u}, {0x2c,1u}, {0x30,0x800f4240u},
                             {0x38,1u}, {0x44,99999u}, {0x60,1u}, {0xb0,0x400f4240u}})
        word(data, clock.first, clock.second);
    supportError(inspectVgm(data), {"YM2151 clock/variant", "Sega PCM", "YM2203 clock/variant", "YMF278B",
        "dual/variant POKEY", "POKEY combined with other chips", "Yamaha combined with Sega/POKEY chips", "YM2413 + YM2612 together"});
    for (quint32 flags : {0u,0x40000000u,0x80000000u,0xc0000000u}) {
        data = vgm(); word(data, 0x50, flags | 3579545);
        const auto h = inspectVgm(data);
        supportError(h, flags == 0x80000000u ? QStringList{"YM3812 clock/variant"} : QStringList{});
        require(h.yamaha.size() == ((flags & 0x40000000u) ? 2 : 1) &&
                h.yamaha[0].stereo == bool(flags & 0x80000000u) && h.yamaha[0].pan == ((flags & 0x80000000u) ? -1 : 0),
                "Dual/variant OPL2 stereo/pan policy changed.");
    }
}
QByteArray extraHeader() {
    auto data = vgm(); word(data, 0x44, 0x40000000u | 3579545);
    word(data, 0xbc, 4); word(data, 0xc0, 12); word(data, 0xc4, 8); word(data, 0xc8, 10);
    data[0xcc] = 1; data[0xcd] = 6; word(data, 0xce, 3000000);
    data[0xd2] = 1; data[0xd3] = char(0x86); data[0xd4] = 0;
    qToLittleEndian<quint16>(0x8180, data.data() + 0xd5);
    return data;
}
void checkExtraHeader() {
    const auto original = extraHeader(); const auto h = inspectVgm(original);
    require(h.headerEnd == 0xc0 && h.dataOffset == 0x100 && h.yamaha.size() == 2 &&
            h.yamaha[1].clock == 3000000 && h.yamaha[0].clock == 3579545 &&
            h.yamaha[0].ssgGain == 1.5 && h.yamaha[0].gain == 1 && h.unsupported.isEmpty(),
            "Alternate clock/paired-volume parsing changed.");
    constexpr auto error = "Invalid VGM chip clock/volume header.";
    for (const auto &field : {std::pair{0xc0,3u}, {0xc0,65u}, {0xc4,4u}, {0xc8,0xffffffffu}, {0xce,99999u}, {0xce,20000001u}}) {
        auto data = original; word(data, field.first, field.second); reject(data, error);
    }
    auto data = original; data[0xcc] = 20; reject(data, error);
    data = original; data[0xcd] = 3; reject(data, error); // No second YM2151.
    data = original; data[0xd4] = 2; reject(data, error); // Invalid instance flags.
    data = original; word(data, 0x44, 3579545); reject(data, error); // Alternate clock needs instance 1.
    data = original; word(data, 0xc0, 4); // Short extra header does not expose trailing pointers.
    require(inspectVgm(data).yamaha[1].clock == 3579545, "Short extra-header fields became visible.");
    data = original; word(data, 0x44, 0); // Non-Yamaha routes do not inspect Yamaha extra tables.
    word(data, 0xc0, 0xffffffffu);
    require(inspectVgm(data).yamaha.isEmpty(), "Non-Yamaha extra-header policy changed.");
    data = original; word(data, 0xc4, 0); data[0xd2] = 2;
    data[0xd3] = 6; data[0xd4] = 0; qToLittleEndian<quint16>(384, data.data() + 0xd5);
    data[0xd7] = 6; data[0xd8] = 0; qToLittleEndian<quint16>(0x8200, data.data() + 0xd9);
    require(inspectVgm(data).yamaha[0].gain == 3, "Ordered absolute/relative volume updates changed.");
}
void checkBounds() {
    reject(QByteArray("Vgm "), "Invalid VGM header.");
    reject(QByteArray(64, '\0'), "Invalid VGM header.");
    auto data = vgm(); word(data, 0x34, 1); reject(data, "Invalid VGM data offset.");
    data = vgm(); word(data, 0x34, 0xffffffffu); reject(data, "Invalid VGM data offset.");
    data = vgm(); word(data, 0xbc, 1); reject(data, "Invalid VGM extra header.");
    data = vgm(); word(data, 0xbc, 0x44); reject(data, "Invalid VGM extra header.");
    data = vgm(); word(data, 0x44, 3579545); word(data, 0xbc, 0x43);
    reject(data, "Invalid VGM chip clock/volume header.");
    data = vgm(); word(data, 0x34, data.size() - 0x34);
    require(inspectVgm(data).dataOffset == data.size(), "Data offset at EOF changed.");
}
void checkRegisterCommands() {
    struct Expected { int opcode, id, instance, base; };
    const Expected expected[] = {
        {0x54,3,0,0}, {0x55,6,0,0}, {0x56,7,0,0}, {0x57,7,0,256},
        {0x58,8,0,0}, {0x59,8,0,256}, {0x5a,9,0,0}, {0x5b,10,0,0},
        {0x5c,11,0,0}, {0x5e,12,0,0}, {0x5f,12,0,256},
        {0xa4,3,1,0}, {0xa5,6,1,0}, {0xa6,7,1,0}, {0xa7,7,1,256},
        {0xa8,8,1,0}, {0xa9,8,1,256}, {0xaa,9,1,0}, {0xab,10,1,0},
        {0xac,11,1,0}, {0xae,12,1,0}, {0xaf,12,1,256}
    };
    for (int opcode = 0; opcode < 256; ++opcode) {
        const auto command = yamahaRegisterCommand(opcode);
        const Expected *match = nullptr;
        for (const auto &test : expected) if (test.opcode == opcode) match = &test;
        if (match) require(command.chipId == match->id && command.instance == match->instance && command.registerBase == match->base,
                           "Yamaha opcode chip/instance/register bank changed.");
        else require(command.chipId == -1, "An unsupported Yamaha opcode became a register write.");
    }
}
void checkVoices() {
    // Fixed snapshots include native indices, role keys AND every displayed label.
    struct Expected { int id; bool variant; int count; const char *hash; };
    const Expected expected[] = {
        {3,false,8,"7e0ec7bf66bb9b0a2f3d532323d529d5c313711ef468989afb013580956b53f7"},
        {6,false,6,"0cfacf8f3e93482df97c16773e5d47fec3ac9a2674f1f175f14c16ff128a5db6"},
        {7,false,16,"13d83a958c7245917f03d02e306474f7c080089e177d606945de50ec3bacfc8a"},
        {8,false,14,"e7d36f74c96439244359185e8c6e28817fe7842eb33bcd53d011885092a3434f"},
        {8,true,16,"8206f1ae9459a86e72e004817fb6a039d726a040d8078416d437d13b198f8741"},
        {9,false,9,"cdde9ead72fad2881cca9f48b6b2f2dd4d4adfcb4c3bd32bcf4582a63a0e6e44"},
        {10,false,9,"cdde9ead72fad2881cca9f48b6b2f2dd4d4adfcb4c3bd32bcf4582a63a0e6e44"},
        {11,false,10,"8a197920f111c355f5c64aa890766c1f01da32465ec978838c736ed210b79042"},
        {12,false,18,"438cd8e9b2eae67cbdcdedc8915ba66b06392af8320cc113690d28a34ec06e85"},
        {0,false,9,"2bc80b5d8db2e07aaeb83f7c04399df088e5c3a85f0275ff9a3e72f39edcff08"},
        {-1,false,9,"2bc80b5d8db2e07aaeb83f7c04399df088e5c3a85f0275ff9a3e72f39edcff08"},
        {13,true,9,"cdde9ead72fad2881cca9f48b6b2f2dd4d4adfcb4c3bd32bcf4582a63a0e6e44"},
    };
    for (const auto &test : expected) {
        VgmYamahaChip chip; chip.id = test.id; chip.variant = test.variant;
        QByteArray snapshot; const auto voices = yamahaVoices(chip);
        for (const auto &voice : voices)
            snapshot += QByteArray::number(voice.index) + '|' + voice.key.toUtf8() + '|' + voice.name.toUtf8() + '\n';
        require(voices.size() == test.count && yamahaVoiceCount(chip) == test.count &&
                QCryptographicHash::hash(snapshot, QCryptographicHash::Sha256).toHex() == test.hash,
                "Yamaha native voice index, role, label or fallback changed.");
    }
}
}
void checkVgm(QApplication &, const QString &, QTextStream &log) {
    checkBounds(); checkHeaderClocks(); checkExtraHeader(); checkRegisterCommands(); checkVoices();
    log << "VGM: clock versions/bounds, chip/variant descriptions, ordered exact errors, extra-header clocks/volumes and fixed native voice/role/label snapshots PASS\n";
    log.flush();
}

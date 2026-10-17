#include "soundconfig.h"
#include <QApplication>
#include <QTextStream>
#include <QtEndian>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
QStringList voices(int count) {
    QStringList result;
    for (int i = 0; i < count; ++i) result << QString("Voice %1").arg(i + 1);
    return result;
}
void word(QByteArray &data, int at, quint32 value) { qToLittleEndian(value, data.data() + at); }
QByteArray nsf(int flags) {
    QByteArray data(128, '\0'); data.replace(0, 5, "NESM\x1a"); data[0x7b] = char(flags);
    return data;
}
QByteArray nsfe(int flags) {
    auto data = QByteArray::fromHex("4e53464508000000494e464f0000000000000000000000004e454e44");
    data[19] = char(flags); return data;
}
QByteArray vgmWith(std::initializer_list<std::pair<int, quint32>> clocks, quint32 version = 0x171) {
    QByteArray data(0x101, '\0'); data.replace(0, 4, "Vgm "); data[0x100] = char(0x66);
    word(data, 4, data.size() - 4); word(data, 8, version);
    if (version >= 0x150) word(data, 0x34, 0xcc);
    for (const auto &clock : clocks) word(data, clock.first, clock.second);
    return data;
}
void expect(const char *label, const QByteArray &data, const QStringList &voiceNames,
            const char *family, const char *name, const char *sources, const char *id) {
    const auto actual = soundConfiguration(data, voiceNames);
    QStringList expectedSources;
    if (sources) expectedSources = QString::fromUtf8(sources).split('|', Qt::SkipEmptyParts);
    else for (int i = 0; i < voiceNames.size(); ++i) expectedSources << QString(id) + ':' + QString::number(i);
    if (actual.id != id || actual.family != family || actual.name != name || actual.sources != expectedSources)
        throw std::runtime_error((QString("Sound configuration changed: %1 (ID %2)").arg(label, actual.id)).toStdString());
}
}

void checkSoundConfigurations(QApplication &, const QString &, QTextStream &log) {
    // Fixed identities/roles were checked against the implementation before refactoring.
    expect("Base NSF", nsf(0), voices(5), "nes", "NES APU",
           "nes.apu:0|nes.apu:1|nes.apu:2|nes.apu:3|nes.apu:4",
           "c6028df6dff979da5b0e853d0cfc1a8f3931690616af9eeaf8401039270584ca");
    expect("Base NSFE", nsfe(0), voices(5), "nes", "NES APU",
           "nes.apu:0|nes.apu:1|nes.apu:2|nes.apu:3|nes.apu:4",
           "c6028df6dff979da5b0e853d0cfc1a8f3931690616af9eeaf8401039270584ca");
    expect("VRC6 NSF", nsf(1), voices(8), "nes", "NES APU + VRC6",
           "nes.apu:0|nes.apu:1|nes.apu:2|nes.apu:3|nes.apu:4|nes.vrc6:0|nes.vrc6:1|nes.vrc6:2",
           "450e407867550084db4da44058c87455f1dcdea48ffdd382992a739dc76954d9");
    expect("N163 NSF", nsf(16), voices(13), "nes", "NES APU + N163",
           "nes.apu:0|nes.apu:1|nes.apu:2|nes.apu:3|nes.apu:4|nes.n163:0|nes.n163:1|nes.n163:2|nes.n163:3|"
           "nes.n163:4|nes.n163:5|nes.n163:6|nes.n163:7",
           "bd9d4a3e16b10af8f9d918390e90a71fdaed1b58c9e54f1e8ae79b1140d252e8");
    expect("5B NSF", nsf(32), voices(8), "nes", "NES APU + 5B",
           "nes.apu:0|nes.apu:1|nes.apu:2|nes.apu:3|nes.apu:4|nes.s5b:0|nes.s5b:1|nes.s5b:2",
           "dec793b241a972556bc4eb8f5c49fb02ed812d1da25245faec12509eb4b46e6c");
    expect("FDS NSF", nsf(4), voices(6), "nes", "NES APU + FDS",
           "nes.apu:0|nes.apu:1|nes.apu:2|nes.apu:3|nes.apu:4|nes.fds:0",
           "0f1d984dd9f5cf4e521c5f26ac0e771a7109638ca2b022cdc50a85b41622c39a");
    expect("MMC5 NSF", nsf(8), voices(8), "nes", "NES APU + MMC5",
           "nes.apu:0|nes.apu:1|nes.apu:2|nes.apu:3|nes.apu:4|nes.mmc5:0|nes.mmc5:1|nes.mmc5:2",
           "b81a123016fec631e9d6cad0b02e3f1a9efd6afca851566464e9e953b1eaf68d");
    expect("VRC7 NSF", nsf(2), voices(11), "nes", "NES APU + VRC7",
           "nes.apu:0|nes.apu:1|nes.apu:2|nes.apu:3|nes.apu:4|nes.vrc7:0|nes.vrc7:1|nes.vrc7:2|nes.vrc7:3|"
           "nes.vrc7:4|nes.vrc7:5",
           "93642148bb6fa2330f9aff2920de3879819b52405d727e595afbc009ebd3594d");
    expect("All NES expansions", nsf(63), voices(29), "nes", "NES APU + VRC6 + N163 + 5B + FDS + MMC5 + VRC7",
           "nes.apu:0|nes.apu:1|nes.apu:2|nes.apu:3|nes.apu:4|nes.vrc6:0|nes.vrc6:1|nes.vrc6:2|nes.n163:0|"
           "nes.n163:1|nes.n163:2|nes.n163:3|nes.n163:4|nes.n163:5|nes.n163:6|nes.n163:7|nes.s5b:0|nes.s5b:1|"
           "nes.s5b:2|nes.fds:0|nes.mmc5:0|nes.mmc5:1|nes.mmc5:2|nes.vrc7:0|nes.vrc7:1|nes.vrc7:2|nes.vrc7:3|"
           "nes.vrc7:4|nes.vrc7:5",
           "74901d946df14a0ed0ff87c3d68cb362aa45c35ed4ed79d7ab956e13db02796a");
    expect("All expansions NSFE", nsfe(63), voices(29), "nes", "NES APU + VRC6 + N163 + 5B + FDS + MMC5 + VRC7",
           "nes.apu:0|nes.apu:1|nes.apu:2|nes.apu:3|nes.apu:4|nes.vrc6:0|nes.vrc6:1|nes.vrc6:2|nes.n163:0|"
           "nes.n163:1|nes.n163:2|nes.n163:3|nes.n163:4|nes.n163:5|nes.n163:6|nes.n163:7|nes.s5b:0|nes.s5b:1|"
           "nes.s5b:2|nes.fds:0|nes.mmc5:0|nes.mmc5:1|nes.mmc5:2|nes.vrc7:0|nes.vrc7:1|nes.vrc7:2|nes.vrc7:3|"
           "nes.vrc7:4|nes.vrc7:5",
           "74901d946df14a0ed0ff87c3d68cb362aa45c35ed4ed79d7ab956e13db02796a");
    expect("Unknown NES flags", nsf(128), voices(5), "nes", "NES APU (other layout)",
           nullptr, "200591c8456a10b404ae0087e3363dac2851bbc366ceaa16c116e36f702c639e");
    expect("Short NSF", QByteArray("NESM\x1a"), voices(2), "nes", "NES APU (other layout)",
           nullptr, "6521fff9f11618b2be8500b345d61b3825fe75fcf81da3b87b8de727779424ef");
    expect("Truncated NSFE INFO", QByteArray::fromHex("4e53464508000000494e464f000000"), voices(2), "nes", "NES APU (other layout)",
           nullptr, "6521fff9f11618b2be8500b345d61b3825fe75fcf81da3b87b8de727779424ef");
    expect("Empty unknown NSFE layout", QByteArray("NSFE"), voices(0), "nes", "NES APU",
           "",
           "de6b7f134a45c59fdffe186d8dc191f2e2291595dd3a213953b987d2ceeb309b");
    expect("Opaque UTF-8 names", nsf(0), QStringList{"Pulse 雪", "Noise"}, "nes", "NES APU (other layout)",
           nullptr, "317013f05887d01e4d9ba9a3faf9f8439ca8d29400b6594a1fcae56bb607fa32");
    expect("SNES DSP", QByteArray("SNES-SPC700"), voices(8), "spc", "SNES DSP",
           "snes.dsp:0|snes.dsp:1|snes.dsp:2|snes.dsp:3|snes.dsp:4|snes.dsp:5|snes.dsp:6|snes.dsp:7",
           "6dbc9df94b131cfb5ded779362f5a801641d92dccfc55449cc6be535a93b08bd");
    expect("Game Boy APU", QByteArray("GBS"), voices(4), "gbs", "Game Boy APU",
           "gb.square:0|gb.square:1|gb.wave:0|gb.noise:0",
           "f023ec6d3f2c044590068d88bf75dfa3bb9c0bd1d4224733efdbb62a5081ddb5");
    expect("AY + Beeper", QByteArray("ZXAYEMUL"), voices(4), "ay", "AY + Beeper",
           "ay.tone:0|ay.tone:1|ay.tone:2|ay.beeper:0",
           "c10f2e29e78a70dafdefe8464db2ff6b9ad19cdc7ee97548cf87ded218d07086");
    expect("PSG", vgmWith({{12,3579545}}), voices(4), "psg-opll", "SN76489 PSG",
           "sn76489.tone:0|sn76489.tone:1|sn76489.tone:2|sn76489.noise:0",
           "9dfc5f24139ba735d21245f4394e58097aeacf5ef0831a56d792cf1611f2b5cd");
    expect("Clockless PSG fallback", vgmWith({}), voices(4), "psg-opll", "SN76489 PSG",
           "sn76489.tone:0|sn76489.tone:1|sn76489.tone:2|sn76489.noise:0",
           "9dfc5f24139ba735d21245f4394e58097aeacf5ef0831a56d792cf1611f2b5cd");
    expect("Changed PSG clock", vgmWith({{12,4000000}}), voices(4), "psg-opll", "SN76489 PSG",
           "sn76489.tone:0|sn76489.tone:1|sn76489.tone:2|sn76489.noise:0",
           "9dfc5f24139ba735d21245f4394e58097aeacf5ef0831a56d792cf1611f2b5cd");
    expect("OPLL", vgmWith({{12,3579545},{16,3579545}}), voices(10), "psg-opll", "YM2413 + PSG",
           "ym2413.fm:0|ym2413.fm:1|ym2413.fm:2|ym2413.fm:3|ym2413.fm:4|ym2413.fm:5|ym2413.fm:6|ym2413.fm:7|"
           "ym2413.fm:8|sn76489.mix:0",
           "1097ec3dafeb5b965226dac34921d5531f71a96ae20e0997894aa99b2f170c92");
    expect("OPN2", vgmWith({{12,3579545},{44,7670454}}), voices(8), "opn2", "YM2612 + PSG",
           "ym2612.fm:0|ym2612.fm:1|ym2612.fm:2|ym2612.fm:3|ym2612.fm:4|ym2612.fm:5|ym2612.dac:0|sn76489.mix:0",
           "80893bf5839df491813a8f7a12ad74e22ce6eb2361d3c5f8d0b4d2f6e8ba4fe0");
    expect("Legacy raw OPN2 clock", vgmWith({{44,7670454}},0x100), voices(8), "opn2", "YM2612 + PSG (other layout)",
           nullptr, "a2bc09b7024033e600a6e2fa0a40dd0dc3bda77177422ccd255c56a2c651b6e1");
    expect("Dual PSG opaque", vgmWith({{12,0x40000000u|3579545}}), voices(8), "psg-opll", "SN76489 PSG (other layout)",
           nullptr, "20459a28c5757bd8fb6c61a8a227891000c5c36d18ba59d7759f813d541d37f9");
    expect("T6W28 opaque", vgmWith({{12,0xc0000000u|3579545}}), voices(4), "psg-opll", "SN76489 PSG (other layout)",
           nullptr, "a86de949bd58ae7f170c75725b569b326e37bbab5193ef7080616d4152423c00");
    expect("Mixed Sega unsupported by decoder", vgmWith({{16,3579545},{44,7670454}}), voices(8), "opn2", "YM2612 + PSG (other layout)",
           nullptr, "7923fde22926539f218116e6c42473e5171afa3f392afe49da7b114a26ad04e3");
    expect("Empty PSG layout", vgmWith({}), voices(0), "psg-opll", "SN76489 PSG",
           "",
           "4885f9ed3bfc2d50b0417ffbebb6b243ccc46b2ff1b4058a1a167acfaee3f6f2");
    expect("Unrelated unsupported VGM chip", vgmWith({{12,3579545},{0x84,1789773}}), voices(4), "psg-opll", "SN76489 PSG",
           "sn76489.tone:0|sn76489.tone:1|sn76489.tone:2|sn76489.noise:0",
           "9dfc5f24139ba735d21245f4394e58097aeacf5ef0831a56d792cf1611f2b5cd");
    expect("POKEY", vgmWith({{0xb0,1773447}}), voices(4), "pokey", "POKEY",
           "pokey.voice:0|pokey.voice:1|pokey.voice:2|pokey.voice:3",
           "3b0e09e41dba356e94b627e0d321f2e3d19924c8571d78359584bf7396c6c8a4");
    expect("Unsupported dual POKEY identity", vgmWith({{0xb0,0x40000000u|1773447}}), voices(4), "pokey", "POKEY",
           "pokey.voice:0|pokey.voice:1|pokey.voice:2|pokey.voice:3",
           "3b0e09e41dba356e94b627e0d321f2e3d19924c8571d78359584bf7396c6c8a4");
    expect("YM2151", vgmWith({{48,3579545}}), voices(8), "yamaha-vgm", "YM2151",
           "ym2151.0.fm:0|ym2151.0.fm:1|ym2151.0.fm:2|ym2151.0.fm:3|ym2151.0.fm:4|ym2151.0.fm:5|ym2151.0.fm:6|"
           "ym2151.0.fm:7",
           "2ba8c1c61b08813360671ad3bb9d7b972eb56c79307ba8383a924ac0e376376e");
    expect("YM2203", vgmWith({{68,3579545}}), voices(6), "yamaha-vgm", "YM2203",
           "ym2203.0.fm:0|ym2203.0.fm:1|ym2203.0.fm:2|ym2203.0.ssg:0|ym2203.0.ssg:1|ym2203.0.ssg:2",
           "0451050f5af1eb03a941224604473b86b167ed0c5a90a29e618b38b7bd709f46");
    expect("YM2608", vgmWith({{72,3579545}}), voices(16), "yamaha-vgm", "YM2608",
           "ym2608.0.fm:0|ym2608.0.fm:1|ym2608.0.fm:2|ym2608.0.fm:3|ym2608.0.fm:4|ym2608.0.fm:5|ym2608.0.ssg:0|"
           "ym2608.0.ssg:1|ym2608.0.ssg:2|ym2608.0.adpcm-a:0|ym2608.0.adpcm-a:1|ym2608.0.adpcm-a:2|"
           "ym2608.0.adpcm-a:3|ym2608.0.adpcm-a:4|ym2608.0.adpcm-a:5|ym2608.0.adpcm-b:0",
           "bc647ff27631cb638dd2b1988cc68bfd4a5dc2ce340667f7038cdb1aba3de30f");
    expect("YM2610", vgmWith({{76,3579545}}), voices(14), "yamaha-vgm", "YM2610",
           "ym2610.0.fm:1|ym2610.0.fm:2|ym2610.0.fm:4|ym2610.0.fm:5|ym2610.0.ssg:0|ym2610.0.ssg:1|"
           "ym2610.0.ssg:2|ym2610.0.adpcm-a:0|ym2610.0.adpcm-a:1|ym2610.0.adpcm-a:2|ym2610.0.adpcm-a:3|"
           "ym2610.0.adpcm-a:4|ym2610.0.adpcm-a:5|ym2610.0.adpcm-b:0",
           "a15afeb736c4e24ad2ae943cdde8fd0cbe187b8617abe15693942897f55e9e34");
    expect("YM3812", vgmWith({{80,3579545}}), voices(9), "yamaha-vgm", "YM3812",
           "ym3812.0.fm:0|ym3812.0.fm:1|ym3812.0.fm:2|ym3812.0.fm:3|ym3812.0.fm:4|ym3812.0.fm:5|ym3812.0.fm:6|"
           "ym3812.0.fm:7|ym3812.0.fm:8",
           "4dd6b4417a4403ebadbf6b93f2256a135f17379dd2f45c1acb3d7dbccf323763");
    expect("YM3526", vgmWith({{84,3579545}}), voices(9), "yamaha-vgm", "YM3526",
           "ym3526.0.fm:0|ym3526.0.fm:1|ym3526.0.fm:2|ym3526.0.fm:3|ym3526.0.fm:4|ym3526.0.fm:5|ym3526.0.fm:6|"
           "ym3526.0.fm:7|ym3526.0.fm:8",
           "0fbd05eba488a34018f9dbf99e95f1e06edfaba832a17c06491fd04b5af27d87");
    expect("Y8950", vgmWith({{88,3579545}}), voices(10), "yamaha-vgm", "Y8950",
           "y8950.0.fm:0|y8950.0.fm:1|y8950.0.fm:2|y8950.0.fm:3|y8950.0.fm:4|y8950.0.fm:5|y8950.0.fm:6|"
           "y8950.0.fm:7|y8950.0.fm:8|y8950.0.adpcm-b:0",
           "c2636862799a6b1c323afe6cafd323735d7fc38f832682bada622acaaec87194");
    expect("YMF262", vgmWith({{92,3579545}}), voices(18), "yamaha-vgm", "YMF262",
           "ymf262.0.fm:0|ymf262.0.fm:1|ymf262.0.fm:2|ymf262.0.fm:3|ymf262.0.fm:4|ymf262.0.fm:5|ymf262.0.fm:6|"
           "ymf262.0.fm:7|ymf262.0.fm:8|ymf262.0.fm:9|ymf262.0.fm:10|ymf262.0.fm:11|ymf262.0.fm:12|"
           "ymf262.0.fm:13|ymf262.0.fm:14|ymf262.0.fm:15|ymf262.0.fm:16|ymf262.0.fm:17",
           "0a0b9bfd247ac9d1493e97f9bafdddb2027cf57f9a4d31e89f44f4d36a9ff230");
    expect("YM2610B", vgmWith({{0x4c,0x80000000u|3579545}}), voices(16), "yamaha-vgm", "YM2610B",
           "ym2610b.0.fm:0|ym2610b.0.fm:1|ym2610b.0.fm:2|ym2610b.0.fm:3|ym2610b.0.fm:4|ym2610b.0.fm:5|"
           "ym2610b.0.ssg:0|ym2610b.0.ssg:1|ym2610b.0.ssg:2|ym2610b.0.adpcm-a:0|ym2610b.0.adpcm-a:1|"
           "ym2610b.0.adpcm-a:2|ym2610b.0.adpcm-a:3|ym2610b.0.adpcm-a:4|ym2610b.0.adpcm-a:5|ym2610b.0.adpcm-b:0",
           "8d5074759efdb4823d45ef9f76762e694d1a3ef0616ea62df1fe1d2050b427cf");
    expect("Dual OPL2 pan", vgmWith({{0x50,0xc0000000u|3579545}}), voices(18), "yamaha-vgm", "YM3812 + YM3812",
           "ym3812.0.fm:0|ym3812.0.fm:1|ym3812.0.fm:2|ym3812.0.fm:3|ym3812.0.fm:4|ym3812.0.fm:5|ym3812.0.fm:6|"
           "ym3812.0.fm:7|ym3812.0.fm:8|ym3812.1.fm:0|ym3812.1.fm:1|ym3812.1.fm:2|ym3812.1.fm:3|ym3812.1.fm:4|"
           "ym3812.1.fm:5|ym3812.1.fm:6|ym3812.1.fm:7|ym3812.1.fm:8",
           "67b4da91bdc6f3fbd75fab8dfb20207bd2855af25d294d25483d8accafa98cea");
    expect("Mixed Yamaha order", vgmWith({{0x44,3579545},{0x30,3579545}}), voices(14), "yamaha-vgm", "YM2151 + YM2203",
           "ym2151.0.fm:0|ym2151.0.fm:1|ym2151.0.fm:2|ym2151.0.fm:3|ym2151.0.fm:4|ym2151.0.fm:5|ym2151.0.fm:6|"
           "ym2151.0.fm:7|ym2203.0.fm:0|ym2203.0.fm:1|ym2203.0.fm:2|ym2203.0.ssg:0|ym2203.0.ssg:1|ym2203.0.ssg:2",
           "4dabf0cbae7161ee74e36a1eefb30ee53414b51c560d68758dbf56802ee32a7e");
    expect("Yamaha opaque names", vgmWith({{0x44,3579545}}), QStringList{"Pulse 雪", "Noise"}, "yamaha-vgm", "YM2203 (other layout)",
           nullptr, "ff65881588158b3fb84ee6683e6cda52f8f6a772ecbd9ac2af97f09ccab30156");
    expect("Dual YM2203", vgmWith({{0x44,0x40000000u|3579545}}), voices(12), "yamaha-vgm", "YM2203 + YM2203",
           "ym2203.0.fm:0|ym2203.0.fm:1|ym2203.0.fm:2|ym2203.0.ssg:0|ym2203.0.ssg:1|ym2203.0.ssg:2|"
           "ym2203.1.fm:0|ym2203.1.fm:1|ym2203.1.fm:2|ym2203.1.ssg:0|ym2203.1.ssg:1|ym2203.1.ssg:2",
           "81f8bf5290bed6278d34db59dac7207541b66911b3ad7c42ee810a1f8f219497");

    auto extra = vgmWith({{0x44,0x40000000u|3579545}});
    const auto original = soundConfiguration(extra, voices(12));
    word(extra, 0xbc, 4); word(extra, 0xc0, 12);
    word(extra, 0xc4, 8); word(extra, 0xc8, 10);
    extra[0xcc] = 1; extra[0xcd] = 6; word(extra, 0xce, 3000000);
    extra[0xd2] = 1; extra[0xd3] = char(0x86); extra[0xd4] = 0;
    qToLittleEndian<quint16>(0x8180, extra.data() + 0xd5);
    const auto changed = soundConfiguration(extra, voices(12));
    require(changed.id == original.id && changed.sources == original.sources && changed.name == original.name,
            "VGM extra clocks/gains affect preset identity.");
    const auto unknown = soundConfiguration(QByteArray("unrecognized"), voices(4));
    require(unknown.id.isEmpty() && unknown.family.isEmpty() && unknown.name.isEmpty() && unknown.sources.isEmpty(),
            "Unknown format acquired a sound configuration.");
    auto reject = [&](const QByteArray &data, const char *error) {
        try { soundConfiguration(data, voices(4)); }
        catch (const std::exception &e) { require(QByteArray(e.what()) == error, "VGM configuration error changed."); return; }
        throw std::runtime_error("Malformed VGM configuration accepted.");
    };
    reject(QByteArray("Vgm "), "Invalid VGM header.");
    auto bad = vgmWith({}); word(bad, 0x34, 1);
    reject(bad, "Invalid VGM data offset.");
    bad = vgmWith({}); word(bad, 0xbc, 1);
    reject(bad, "Invalid VGM extra header.");
    word(extra, 0xc8, 0xffffffffu);
    reject(extra, "Invalid VGM chip clock/volume header.");
    log << "Sound configurations: fixed IDs/names/roles, NES expansion order, opaque UTF-8 layouts, Sega/POKEY/Yamaha variants, extra clocks/gains and malformed-input policy PASS\n";
    log.flush();
}

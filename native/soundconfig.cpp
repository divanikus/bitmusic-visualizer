#include "soundconfig.h"
#include "vgm.h"
#include <QCryptographicHash>
#include <utility>

namespace {
quint32 word(const QByteArray &data, int at) {
    if (at < 0 || at > data.size() - 4) return 0;
    quint32 result = 0;
    for (int i = 0; i < 4; ++i) result |= quint32(quint8(data.at(at + i))) << (8 * i);
    return result;
}
void addSources(SoundConfiguration &c, const QString &chip, int count) {
    for (int i = 0; i < count; ++i) c.sources << chip + ':' + QString::number(i);
}
SoundConfiguration finishConfiguration(SoundConfiguration c, const QString &hardware, const QStringList &voices) {
    const bool opaque = c.sources.size() != voices.size();
    // These exact UTF-8 fields, separators and order are persisted preset keys.
    const auto identity = c.family + '|' + hardware + '|' + (opaque ? voices : c.sources).join('|');
    c.id = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (opaque) {
        c.name += " (other layout)";
        c.sources.clear();
        addSources(c, c.id, int(voices.size())); // Exact matching only for unverified mappings.
    }
    return c;
}
SoundConfiguration nesConfiguration(const QByteArray &data, const QStringList &voices) {
    int flags = -1;
    if (data.startsWith("NESM\x1a") && data.size() >= 128) flags = quint8(data[0x7b]);
    else for (qsizetype at = 4; at <= data.size() - 8;) {
        const auto size = word(data, int(at));
        if (size > quint64(data.size() - at - 8)) break;
        if (data.mid(at + 4, 4) == "INFO" && size >= 8) { flags = quint8(data[at + 15]); break; }
        at += 8 + qsizetype(size);
    }
    SoundConfiguration c;
    c.family = "nes"; c.name = "NES APU";
    addSources(c, "nes.apu", 5);
    // Backend order is not flag-bit order. Roles avoid ambiguous Square 3 labels.
    struct Expansion { int bit, count; const char *key, *name; };
    for (const auto &e : {Expansion{1,3,"vrc6","VRC6"}, {16,8,"n163","N163"},
            {32,3,"s5b","5B"}, {4,1,"fds","FDS"}, {8,3,"mmc5","MMC5"}, {2,6,"vrc7","VRC7"}})
        if (flags >= 0 && (flags & e.bit)) {
            addSources(c, QString("nes.") + e.key, e.count);
            c.name += QString(" + ") + e.name;
        }
    if (flags < 0 || (flags & ~63)) c.sources.clear();
    return finishConfiguration(std::move(c), QString::number(flags), voices);
}
SoundConfiguration segaConfiguration(const QByteArray &data, const QStringList &voices) {
    SoundConfiguration c;
    // Keep raw header words, even for legacy versions: they are part of existing IDs.
    const auto psg = word(data, 12), opll = word(data, 16), opn2 = word(data, 44), other = word(data, 48);
    c.family = opn2 ? "opn2" : "psg-opll";
    auto signature = [](quint32 value) { return QString::number((value & 0xc0000000u) | (value ? 1u : 0u), 16); };
    QString hardware = signature(psg ? psg : 1) + '-' + signature(opll) + '-' + signature(opn2) + '-' + signature(other);
    c.name = opn2 ? "YM2612 + PSG" : opll ? "YM2413 + PSG" : "SN76489 PSG";
    if (word(data, 8) >= 0x110 && !((psg | opll | opn2) & 0xc0000000u) && !other && !(opll && opn2)) {
        if (opn2 && voices.size() == 8) {
            addSources(c, "ym2612.fm", 6); addSources(c, "ym2612.dac", 1); addSources(c, "sn76489.mix", 1);
        } else if (opll && voices.size() == 10) {
            addSources(c, "ym2413.fm", 9); addSources(c, "sn76489.mix", 1);
        } else if (!opll && !opn2 && voices.size() == 4) {
            addSources(c, "sn76489.tone", 3); addSources(c, "sn76489.noise", 1);
        }
    } else hardware += "-legacy-or-variant";
    return finishConfiguration(std::move(c), hardware, voices);
}
SoundConfiguration yamahaConfiguration(const VgmHeader &header, const QStringList &voices) {
    SoundConfiguration c;
    c.family = "yamaha-vgm";
    QString hardware;
    QStringList names;
    for (const auto &chip : header.yamaha) {
        names << chip.name;
        for (const auto &voice : yamahaVoices(chip))
            c.sources << chip.name.toLower() + QString(".%1.").arg(chip.instance) + voice.key;
        hardware += QString("%1:%2:%3;").arg(chip.id).arg(chip.instance).arg(chip.pan);
        if (chip.variant) hardware += "variant;";
    }
    c.name = names.join(" + ");
    return finishConfiguration(std::move(c), hardware, voices);
}
}

SoundConfiguration vgmSoundConfiguration(const QByteArray &data, const QStringList &voices, const VgmHeader &header) {
    if (!header.yamaha.isEmpty()) return yamahaConfiguration(header, voices);
    if (header.pokeyClock) {
        SoundConfiguration c;
        c.family = "pokey"; c.name = "POKEY"; addSources(c, "pokey.voice", 4);
        return finishConfiguration(std::move(c), {}, voices);
    }
    return segaConfiguration(data, voices);
}
SoundConfiguration soundConfiguration(const QByteArray &data, const QStringList &voices) {
    if (data.startsWith("NESM\x1a") || data.startsWith("NSFE")) return nesConfiguration(data, voices);
    if (data.startsWith("Vgm ")) return vgmSoundConfiguration(data, voices, inspectVgm(data));
    SoundConfiguration c;
    if (data.startsWith("SNES-SPC700")) {
        c.family = "spc"; c.name = "SNES DSP"; addSources(c, "snes.dsp", 8);
    } else if (data.startsWith("GBS")) {
        c.family = "gbs"; c.name = "Game Boy APU";
        addSources(c, "gb.square", 2); addSources(c, "gb.wave", 1); addSources(c, "gb.noise", 1);
    } else if (data.startsWith("ZXAYEMUL")) {
        c.family = "ay"; c.name = "AY + Beeper"; addSources(c, "ay.tone", 3); addSources(c, "ay.beeper", 1);
    } else return c;
    return finishConfiguration(std::move(c), {}, voices);
}

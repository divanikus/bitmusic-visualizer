#include "soundconfig.h"
#include "vgm.h"
#include <QCryptographicHash>

SoundConfiguration soundConfiguration(const QByteArray &data, const QStringList &voices) {
    SoundConfiguration c;
    QString hardware;
    auto add = [&](const QString &chip, int count) {
        for (int i = 0; i < count; ++i) c.sources << chip + ':' + QString::number(i);
    };
    auto word = [&](int at) -> quint32 {
        if (at < 0 || at > data.size()-4) return 0;
        quint32 result = 0;
        for (int i = 0; i < 4; ++i) result |= quint32(quint8(data.at(at+i))) << (8*i);
        return result;
    };
    if (data.startsWith("NESM\x1a") || data.startsWith("NSFE")) {
        int flags = -1;
        if (data.startsWith("NESM\x1a") && data.size() >= 128) flags = quint8(data[0x7b]);
        else for (qsizetype at = 4; at <= data.size()-8;) {
            const auto size = word(int(at));
            if (size > quint64(data.size()-at-8)) break;
            if (data.mid(at+4, 4) == "INFO" && size >= 8) { flags = quint8(data[at+15]); break; }
            at += 8 + qsizetype(size);
        }
        c.family = "nes"; c.name = "NES APU"; hardware = QString::number(flags);
        add("nes.apu", 5);
        // Backend order is not flag-bit order. Roles avoid ambiguous Square 3 labels.
        struct Expansion { int bit, count; const char *key, *name; };
        for (const auto &e : {Expansion{1,3,"vrc6","VRC6"}, {16,8,"n163","N163"},
                {32,3,"s5b","5B"}, {4,1,"fds","FDS"}, {8,3,"mmc5","MMC5"}, {2,6,"vrc7","VRC7"}})
            if (flags >= 0 && (flags & e.bit)) { add(QString("nes.")+e.key, e.count); c.name += QString(" + ")+e.name; }
        if (flags < 0 || (flags & ~63)) c.sources.clear();
    } else if (data.startsWith("Vgm ") && !inspectVgm(data).yamaha.isEmpty()) {
        const auto chips = inspectVgm(data).yamaha;
        QStringList names;
        c.family = "yamaha-vgm";
        for (const auto &chip : chips) {
            names << chip.name;
            for(const auto &voice:yamahaVoices(chip))
                c.sources << chip.name.toLower()+QString(".%1.").arg(chip.instance)+voice.key;
            hardware += QString("%1:%2:%3;").arg(chip.id).arg(chip.instance).arg(chip.pan);
            if (chip.variant) hardware += "variant;";
        }
        c.name = names.join(" + ");
    } else if (data.startsWith("Vgm ") && inspectVgm(data).pokeyClock) {
        c.family = "pokey"; c.name = "POKEY"; add("pokey.voice", 4);
    } else if (data.startsWith("Vgm ")) {
        const auto psg = word(12), opll = word(16), opn2 = word(44), other = word(48);
        c.family = opn2 ? "opn2" : "psg-opll";
        auto signature = [](quint32 value) { return QString::number((value & 0xc0000000u) | (value ? 1u : 0u), 16); };
        hardware = signature(psg ? psg : 1) + '-' + signature(opll) + '-' + signature(opn2) + '-' + signature(other);
        c.name = opn2 ? "YM2612 + PSG" : opll ? "YM2413 + PSG" : "SN76489 PSG";
        if (word(8) >= 0x110 && !((psg | opll | opn2) & 0xc0000000u) && !other && !(opll && opn2)) {
            if (opn2 && voices.size() == 8) { add("ym2612.fm", 6); add("ym2612.dac", 1); add("sn76489.mix", 1); }
            else if (opll && voices.size() == 10) { add("ym2413.fm", 9); add("sn76489.mix", 1); }
            else if (!opll && !opn2 && voices.size() == 4) { add("sn76489.tone", 3); add("sn76489.noise", 1); }
        } else hardware += "-legacy-or-variant";
    } else if (data.startsWith("SNES-SPC700")) {
        c.family = "spc"; c.name = "SNES DSP"; add("snes.dsp", 8);
    } else if (data.startsWith("GBS")) {
        c.family = "gbs"; c.name = "Game Boy APU"; add("gb.square", 2); add("gb.wave", 1); add("gb.noise", 1);
    } else if (data.startsWith("ZXAYEMUL")) {
        c.family = "ay"; c.name = "AY + Beeper"; add("ay.tone", 3); add("ay.beeper", 1);
    } else return c;
    const bool opaque = c.sources.size() != voices.size();
    const auto identity = c.family + '|' + hardware + '|' + (opaque ? voices : c.sources).join('|');
    c.id = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (opaque) {
        c.name += " (other layout)"; c.sources.clear();
        add(c.id, int(voices.size())); // Exact matching only for unverified mappings.
    }
    return c;
}

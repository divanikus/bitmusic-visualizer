#include "yamahatrack.h"
#include "pokeytrack.h"
#include "romfiles.h"
#include <QApplication>
#include <QTextStream>
#include <QtEndian>
#include <stdexcept>
#include <utility>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void word(QByteArray &data, int at, quint32 value) { qToLittleEndian(value, data.data() + at); }
void appendWord(QByteArray &data, quint32 value) { const auto at = data.size(); data.resize(at + 4); word(data, int(at), value); }
QByteArray fixture(const QByteArray &commands, quint32 ticks = 0, int loop = -1, int chipAt = 0x44) {
    QByteArray data(0x100, '\0'); data.replace(0, 4, "Vgm ");
    word(data, 8, 0x171); word(data, 0x34, 0xcc); word(data, chipAt, 4000000);
    word(data, 0x18, ticks);
    if (loop >= 0) word(data, 0x1c, 0x100 + loop - 0x1c);
    data += commands; word(data, 4, data.size() - 4); return data;
}
std::unique_ptr<MusicTrack> open(QByteArray data, bool playlist = true, bool pokey = false, int song = 0) {
    const auto path = QString::fromUtf8("Fixture 雪.track.vgm");
    if (pokey) {
        word(data, 0x44, 0); word(data, 0xb0, 1773447);
        return std::make_unique<PokeyTrack>(path, std::move(data), song, playlist, false);
    }
    // No test reads the user's rhythm ROM; actual-use snapshots have their own suite.
    return std::make_unique<YamahaTrack>(path, std::move(data), song, playlist, false, std::make_shared<YamahaRoms>());
}
void reject(const QByteArray &data, const char *message, int song = 0) {
    try { open(data, true, false, song); }
    catch (const std::exception &e) {
        if (QByteArray(e.what()) != message)
            throw std::runtime_error((QByteArray("Yamaha load error changed: ") + e.what()).toStdString());
        return;
    }
    throw std::runtime_error("Invalid Yamaha stream accepted.");
}
QByteArray sampleBlock(int type, quint32 total = 1, quint32 offset = 0, QByteArray bytes = {}) {
    QByteArray data = QByteArray::fromHex("6766"); data += char(type);
    appendWord(data, bytes.size() + 8); appendWord(data, total); appendWord(data, offset);
    return data + bytes;
}
void checkErrorsAndTiming() {
    reject({}, "Invalid subsong number.", 1);
    reject({}, "Invalid VGM header.");
    auto data = fixture(QByteArray::fromHex("5d")); word(data, 4, 1);
    reject(data, "Truncated or inconsistent VGM file size.");
    word(data, 12, 3579545);
    reject(data, "Unsupported sound chip in VGM/VGZ: Yamaha combined with Sega/POKEY chips.\nThis player cannot play this chip configuration yet.");
    data = fixture({}); word(data, 0x44, 0);
    reject(data, "No supported Yamaha chip in VGM.");
    for (const auto *hex : {"61", "64", "55", "67", "6766"})
        reject(fixture(QByteArray::fromHex(hex)), "Truncated Yamaha VGM command.");
    reject(fixture(QByteArray::fromHex("64610000")), "Invalid VGM wait override.");
    reject(fixture(QByteArray::fromHex("5d")), "Unsupported command 0x5d in Yamaha VGM.");
    reject(fixture(QByteArray::fromHex("a5000066")), "VGM command targets an undeclared Yamaha chip.");
    reject(fixture({}), "Yamaha VGM has no end command.");
    reject(fixture(QByteArray::fromHex("61010066"), 99, 1), "Invalid or zero-duration VGM loop.");
    reject(fixture(QByteArray::fromHex("61010066"), 99, 3), "Invalid or zero-duration VGM loop.");
    reject(fixture(QByteArray::fromHex("626462e80366"), 0, 0), "Unsupported VGM loop with changing wait lengths.");
    reject(fixture(QByteArray::fromHex("61010066"), 2), "VGM duration disagrees with its command stream.");
    auto changedWait = open(fixture(QByteArray::fromHex("6462e8036266"), 1000, 0));
    require(changedWait->boundary().frame == 1000 && changedWait->boundary().loopFrame == 0,
            "An override before a loop's first wait changed timing.");
    auto waits = open(fixture(QByteArray::fromHex("6101006263707f66"), 1635));
    require(waits->boundary().frame == 1635, "Yamaha wait commands changed duration.");
    const auto commands = QByteArray::fromHex("61225661112b66");
    auto looped = open(fixture(commands, 33075, 3));
    require(looped->boundary().frame == 33075 && looped->boundary().loopFrame == 22050 &&
            looped->info().durationMs == 750 && looped->info().loopStartMs == 500,
            "Yamaha loop boundary or metadata rounding changed.");
    auto unknown = open(fixture(commands));
    require(unknown->boundary().frame == 33075 && unknown->info().durationMs == -1,
            "An omitted header duration lost its unknown metadata policy.");
}
void checkSampleErrorsAndLimits() {
    reject(fixture(QByteArray::fromHex("67000000000000")), "Invalid VGM data-block marker.");
    reject(fixture(QByteArray::fromHex("67660007000000")), "Truncated VGM sample block.");
    reject(fixture(sampleBlock(0)), "Unsupported Yamaha VGM data-block type.");
    reject(fixture(sampleBlock(0x83, 0)), "VGM command targets an undeclared Yamaha chip.");
    constexpr auto rangeError = "Invalid Yamaha VGM sample-memory range (maximum 16 MiB per bank).";
    for (const auto &block : {sampleBlock(0x83, 0), sampleBlock(0x83, 0x1000001),
                             sampleBlock(0x83, 1, 2), sampleBlock(0x83, 1, 1, QByteArray(1, 0))})
        reject(fixture(block, 0, -1, 0x4c), rangeError);
    for (const auto &type : {std::pair{0x81,0x48}, {0x82,0x4c}, {0x83,0x4c}, {0x88,0x58}}) {
        auto track = open(fixture(sampleBlock(type.first, 0x1000000) + char(0x66), 0, -1, type.second));
        require(track->boundary().frame == 0, "A valid zero-length sample image or maximum bank was rejected.");
    }
    const auto block = sampleBlock(0x83);
    QByteArray manyBlocks; manyBlocks.reserve(65537 * block.size());
    for (int i = 0; i < 65536; ++i) manyBlocks += block;
    { auto allowed = open(fixture(manyBlocks + char(0x66), 0, -1, 0x4c)); }
    reject(fixture(manyBlocks + block, 0, -1, 0x4c), "Yamaha VGM has too many sample blocks.");
    QByteArray writes; writes.reserve(6000003);
    for (int i = 0; i < 2000000; ++i) writes += QByteArray::fromHex("550000");
    { auto allowed = open(fixture(writes + char(0x66))); }
    // Event limit precedes undeclared-chip lookup for an otherwise complete register write.
    reject(fixture(writes + QByteArray::fromHex("a50000")), "Yamaha VGM has too many register writes.");
}
QByteArray withTags(QByteArray data, const QStringList &fields) {
    const QString text = fields.join(QChar(0)) + QChar(0);
    QByteArray encoded(text.size() * 2, '\0');
    for (int i = 0; i < text.size(); ++i) qToLittleEndian<quint16>(text[i].unicode(), encoded.data() + 2 * i);
    word(data, 0x14, data.size() - 0x14);
    data += "Gd3 "; appendWord(data, 0xffffffffu); appendWord(data, encoded.size());
    data += encoded; word(data, 4, data.size() - 4); return data;
}
void checkMetadata() {
    const auto base = fixture(QByteArray::fromHex("6144ac66"), 44100);
    const auto tagged = withTags(base, {QString::fromUtf8("  Title 雪  "), "Other title", "", "", "  Console  ", "Other system"});
    for (bool pokey : {false,true}) {
        auto plain = open(base, true, pokey), named = open(tagged, false, pokey);
        const auto &p = plain->info(); const auto &n = named->info();
        require(p.title == QString::fromUtf8("Fixture 雪.track") && p.durationMs == 1000 &&
                p.system == (pokey ? "Atari 8-bit / 5200" : "Yamaha VGM") && p.playlist.size() == 1 &&
                n.title == QString::fromUtf8("Title 雪") && n.system == "Console" && n.playlist.isEmpty() &&
                n.configuration.id == p.configuration.id && n.voices == p.voices && n.path == p.path,
                "VGM GD3 title/system, fallback, selected-only metadata or identity changed.");
        if (!pokey) require(p.configuration.id == "0451050f5af1eb03a941224604473b86b167ed0c5a90a29e618b38b7bd709f46",
                            "Yamaha loading changed its fixed configuration ID.");
        auto blank = open(withTags(base, {"  ", "Japanese title", "", "", "", "Japanese system"}), true, pokey);
        require(blank->info().title == p.title && blank->info().system == p.system,
                "VGM empty English fields changed fallback policy.");
        const int at = base.size();
        for (int kind = 0; kind < 5; ++kind) {
            auto invalid = tagged;
            if (kind == 0) invalid[at] = 'X';
            if (kind == 1) word(invalid, at + 8, 3);
            if (kind == 2) word(invalid, at + 8, 1024 * 1024 + 2);
            if (kind == 3) word(invalid, at + 8, invalid.size());
            if (kind == 4) word(invalid, 0x14, invalid.size() - 8 - 0x14);
            auto ignored = open(invalid, true, pokey);
            require(ignored->info().title == p.title && ignored->info().system == p.system,
                    "Malformed GD3 stopped falling back silently.");
        }
    }
}
}
void checkYamahaLoading(QApplication &, const QString &, QTextStream &log) {
    checkErrorsAndTiming(); checkSampleErrorsAndLimits(); checkMetadata();
    log << "Yamaha loading: exact error/limit precedence, wait/loop timing, sample-bank bounds, GD3/fallbacks, selected metadata and fixed identity PASS\n";
    log.flush();
}

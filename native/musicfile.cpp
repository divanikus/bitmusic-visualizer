#include "engine.h"
#include "sidtrack.h"
#include "pokeytrack.h"
#include "yamahatrack.h"
#include "miditrack.h"
#include "mt32track.h"
#include "trackertrack.h"
#include "ttttrack.h"
#include "tia-replay/replay.h"
#include "vgm.h"
#include <QFile>
#include <zlib.h>
#include <stdexcept>

namespace {
bool hasGmeSignature(const QByteArray &data) {
    return data.startsWith("NESM\x1a") || data.startsWith("NSFE") || data.startsWith("Vgm ") ||
           data.startsWith("SNES-SPC700") || data.startsWith("GBS") || data.startsWith("ZXAYEMUL");
}
}
QByteArray readMusicFile(const QString &path) {
    constexpr int limit = 64 * 1024 * 1024;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
    auto data = file.read(limit + 1);
    if (data.size() > limit) throw std::runtime_error("Music file exceeds 64 MB.");
    if (data.startsWith(QByteArray::fromHex("1f8b"))) {
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef *>(data.data());
        stream.avail_in = static_cast<uInt>(data.size());
        if (inflateInit2(&stream, 15 + 16) != Z_OK) throw std::runtime_error("Cannot initialize gzip decoder.");
        QByteArray decoded;
        std::array<char, 65536> chunk{};
        int result = Z_OK;
        while (result == Z_OK && decoded.size() <= limit) {
            stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
            stream.avail_out = chunk.size();
            result = inflate(&stream, Z_NO_FLUSH);
            decoded.append(chunk.data(), static_cast<int>(chunk.size() - stream.avail_out));
        }
        inflateEnd(&stream);
        if (result != Z_STREAM_END || decoded.size() > limit)
            throw std::runtime_error("Invalid, truncated or oversized gzip music file.");
        data = std::move(decoded);
    }
    if (!(hasGmeSignature(data) || data.startsWith("MThd") || Ttt::recognizes(data) || isTrackerModule(data)))
        throw std::runtime_error("Supported formats: NSF, NSFE, VGM, VGZ, SPC, GBS, AY (ZXAYEMUL), SID (PSID/RSID), MIDI (SMF 0/1), TTT (TIATracker), tracker modules (MOD, XM, S3M, IT, MED and other libxmp formats).");
    return data;
}


std::unique_ptr<MusicTrack> openMusicTrack(const QString &path, int song, bool playlist, bool capture,
                                        std::shared_ptr<const SidRoms> roms, std::shared_ptr<const YamahaRoms> yamahaRoms,
                                        std::shared_ptr<const MidiResources> midiResources, std::shared_ptr<const Mt32Resources> mt32Resources) {
    if (mt32Resources) return std::make_unique<Mt32Track>(path,QByteArray{},song,playlist,capture,std::move(mt32Resources));
    if (midiResources) return std::make_unique<MidiTrack>(path,QByteArray{},song,playlist,capture,std::move(midiResources));
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const auto magic = file.read(4);
        if (magic == "PSID" || magic == "RSID") return std::make_unique<SidTrack>(path, song, playlist, capture, std::move(roms));
    }
    auto data = readMusicFile(path);
    if (Ttt::recognizes(data)) return std::make_unique<TttTrack>(path,std::move(data),song,playlist,capture);
    if (data.startsWith("MThd")) return openMidiTrack(path,std::move(data),song,playlist,capture);
    if (data.startsWith("Vgm ")) {
        const auto header = inspectVgm(data); requireSupportedVgm(header);
        if (!header.yamaha.isEmpty()) return std::make_unique<YamahaTrack>(path, std::move(data), song, playlist, capture, std::move(yamahaRoms));
        if (header.pokeyClock) return std::make_unique<PokeyTrack>(path, std::move(data), song, playlist, capture);
    }
    const bool console = data.startsWith("NESM") || hasGmeSignature(data);
    if (!console) return std::make_unique<TrackerTrack>(path, std::move(data), song, playlist, capture);
    return std::make_unique<GmeTrack>(path, song, playlist, capture, std::move(data));
}

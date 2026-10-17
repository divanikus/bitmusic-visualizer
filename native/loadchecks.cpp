#include "engine.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <zlib.h>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write loading fixture.");
}
QString error(const std::function<void()> &operation) {
    try { operation(); } catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
    return {};
}
QByteArray gzip(const QByteArray &bytes) {
    z_stream stream{};
    require(deflateInit2(&stream, 1, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) == Z_OK, "Cannot initialize gzip fixture.");
    QByteArray packed(int(deflateBound(&stream, uLong(bytes.size()))), '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(bytes.constData()));
    stream.avail_in = uInt(bytes.size());
    stream.next_out = reinterpret_cast<Bytef *>(packed.data()); stream.avail_out = uInt(packed.size());
    const auto result = deflate(&stream, Z_FINISH);
    packed.resize(qsizetype(stream.total_out));
    deflateEnd(&stream);
    require(result == Z_STREAM_END, "Cannot compress loading fixture.");
    return packed;
}
}

void checkMusicLoading(QApplication &, const QString &directory, QTextStream &log) {
    QTemporaryDir files(QDir(directory).absoluteFilePath("loading-XXXXXX"));
    require(files.isValid(), "Cannot isolate loading checks.");
    const auto original = QDir(directory).filePath("demo.nsf");
    const auto bytes = readMusicFile(original);
    const auto renamed = files.filePath("renamed.bin");
    write(renamed, bytes);
    require(readMusicFile(renamed) == bytes && openMusicTrack(renamed)->info().voices.size() == 5,
            "Loading/routing started depending on the file extension.");
    const auto compressed = files.filePath("compressed.bin");
    const auto packed = gzip(bytes);
    write(compressed, packed);
    require(readMusicFile(compressed) == bytes && openMusicTrack(compressed, 1, false)->info().song == 1,
            "Content-detected gzip lost decoded bytes or selected subsong routing.");
    write(compressed, packed + QByteArray("trailing data"));
    require(readMusicFile(compressed) == bytes, "Gzip trailing-byte policy changed.");
    write(compressed, packed + gzip(QByteArray("ignored second member")));
    require(readMusicFile(compressed) == bytes, "Gzip first-member policy changed.");
    const QString gzipError = "Invalid, truncated or oversized gzip music file.";
    write(compressed, packed.left(packed.size() - 4));
    require(error([&] { readMusicFile(compressed); }) == gzipError, "Truncated gzip error changed.");
    auto corrupted = packed; corrupted[corrupted.size() - 8] ^= 1;
    write(compressed, corrupted);
    require(error([&] { readMusicFile(compressed); }) == gzipError, "Gzip checksum error changed.");
    const auto unknown = files.filePath("unknown.bin");
    write(unknown, {});
    const auto unsupported = error([&] { readMusicFile(unknown); });
    require(unsupported.startsWith("Supported formats:") && error([&] { openMusicTrack(unknown); }) == unsupported,
            "Unsupported-file error boundary changed.");
    write(unknown, "PSID");
    require(error([&] { readMusicFile(unknown); }) == unsupported &&
            !error([&] { openMusicTrack(unknown); }).isEmpty() && error([&] { openMusicTrack(unknown); }) != unsupported,
            "Raw SID probe no longer precedes generic loading.");
    write(compressed, gzip("PSID"));
    require(error([&] { openMusicTrack(compressed); }) == unsupported, "Compressed SID acquired new routing.");
    const auto missing = files.filePath("missing.bin");
    require(!error([&] { readMusicFile(missing); }).isEmpty() &&
            error([&] { openMusicTrack(missing); }) == error([&] { readMusicFile(missing); }), "Missing-file errors diverged.");

    constexpr qsizetype limit = 64 * 1024 * 1024;
    QFile large(files.filePath("large.bin"));
    require(large.open(QIODevice::WriteOnly) && large.write("NESM\x1a", 5) == 5 && large.resize(limit), "Cannot write bounded loading fixture.");
    large.close();
    require(readMusicFile(large.fileName()).size() == limit, "Exact music-file size limit changed.");
    require(large.open(QIODevice::WriteOnly | QIODevice::Append) && large.write("x", 1) == 1, "Cannot extend loading fixture.");
    large.close();
    require(error([&] { readMusicFile(large.fileName()); }) == "Music file exceeds 64 MB.", "Oversized music-file error changed.");
    QByteArray expanded(limit + 1, 'x'); expanded.replace(0, 5, QByteArray("NESM\x1a", 5));
    write(compressed, gzip(expanded)); expanded.clear();
    require(error([&] { readMusicFile(compressed); }) == gzipError, "Expanded music-file bound changed.");

    for (int song : {0, 1, 2}) {
        GmeTrack full(QDir(directory).filePath("named.nsfe"), song, true);
        GmeTrack selected(QDir(directory).filePath("named.nsfe"), song, false);
        require(full.info().playlist.size() == 3 && selected.info().playlist.isEmpty() && full.info().song == song &&
                full.info().title == selected.info().title && full.info().durationMs == selected.info().durationMs &&
                full.info().system == selected.info().system && full.info().loopStartMs == selected.info().loopStartMs,
                "Selected-only GME metadata differs from full playlist metadata.");
    }
    auto single = bytes;
    single[6] = 1;
    std::fill(single.begin() + 14, single.begin() + 46, '\0');
    const auto unicode = files.filePath(QString::fromUtf8("Музыка.extra.nsf"));
    GmeTrack filename(unicode, 0, true, false, single);
    require(filename.info().title == QFileInfo(unicode).completeBaseName() && filename.info().durationMs == -1 &&
            filename.info().playlist[0].title == filename.info().title, "Single-song filename/unknown-duration fallback changed.");
    single.replace(14, 15, QByteArray("  Game title   ", 15));
    GmeTrack game(unicode, 0, false, false, single);
    require(game.info().title == "Game title" && game.info().playlist.isEmpty(), "Single-song trimmed game-title fallback changed.");
    GmeTrack multi(original);
    require(multi.info().playlist[0].title == "Track 01" && multi.info().playlist[1].title == "Track 02" &&
            multi.info().playlist[1].durationMs == -1, "Multi-song title/unknown-duration fallback changed.");
    require(error([&] { GmeTrack bad(original, -1); }) == "Invalid subsong number." &&
            error([&] { GmeTrack bad(original, 999); }) == "Invalid subsong number.", "Invalid GME subsong error changed.");
    log << "Music loading: extension-independent routing, gzip content/first-member/errors/size bounds, raw SID precedence, "
           "missing/unsupported errors and selected/full GME metadata PASS\n";
    log.flush();
}

#include "soundfontlist.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringDecoder>
#include <stdexcept>

namespace {
void require(bool ok, const QString &message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}
QStringList entries(const QByteArray &bytes, bool json) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = QString(decoder(bytes)).trimmed();
    require(!decoder.hasError(), "SoundFont list must be UTF-8 text.");
    QStringList result;
    if (json || text.startsWith('{') || text.startsWith('[')) {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(text.toUtf8(), &error);
        require(error.error == QJsonParseError::NoError, "Invalid SoundFont JSON: " + error.errorString());
        const auto root = doc.object();
        require(doc.isObject() && root.size() == 1 && root.value("soundFonts").isArray(),
                "SoundFont JSON must contain a soundFonts array.");
        for (const auto &value : root.value("soundFonts").toArray()) {
            const auto item = value.toObject();
            require(value.isObject() && item.value("fileName").isString(),
                    "Each SoundFont JSON entry needs a fileName string.");
            for (auto it = item.begin(); it != item.end(); ++it) {
                if (it.key() == "fileName") continue;
                const bool bass = it.key() == "gain" || it.key() == "channels" || it.key() == "patchMappings";
                require(false, bass ? "This SoundFont list uses BASSMIDI rules (" + it.key() +
                        "). Gain, channel restrictions and patch mappings are not supported."
                    : "Unsupported SoundFont JSON field: " + it.key());
            }
            result << item.value("fileName").toString();
        }
    } else {
        for (auto line : text.split('\n')) {
            line = line.trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            if (line.startsWith('"') && line.endsWith('"')) line = line.mid(1, line.size()-2);
            result << line;
        }
    }
    require(!result.isEmpty(), "SoundFont list contains no banks.");
    return result;
}

class ListReader {
public:
    QStringList paths;
    void expand(QString path, int depth = 0) {
        require(!path.isEmpty() && path.size() <= 4096 && !path.contains(QChar(0)) && !path.contains('|'),
                "Invalid SoundFont path. Legacy SFLIST mapping syntax is not supported.");
        path.replace('\\', '/'); // Also accept Windows-authored relative lists on Unix.
        const bool bank = path.endsWith(".sf2", Qt::CaseInsensitive);
        require(bank || isSoundFontList(path), "Unsupported SoundFont file: " + QFileInfo(path).fileName() +
                ". Use SF2 banks or SFLIST/JSON lists; SF3, DLS and SFZ are not supported.");
        const QFileInfo info(path);
        const auto identity = info.canonicalFilePath();
        require(info.isFile() && !identity.isEmpty(), "SoundFont file is missing: " + path);
        if (bank) {
            // A repeated unmodified bank has the same presets. Keep its last position.
            paths.removeAll(identity);
            paths << identity;
            require(paths.size() <= 32, "SoundFont list exceeds 32 banks.");
            return;
        }
        require(depth < 8, "SoundFont lists exceed 8 nesting levels.");
        require(!active.contains(identity), "SoundFont list contains a reference cycle: " + info.fileName());
        QFile file(path);
        require(file.open(QIODevice::ReadOnly), "Cannot open SoundFont list: " + info.fileName());
        require(file.size() <= 65536 - textBytes, "SoundFont lists exceed 64 KiB of combined text.");
        const auto bytes = file.read(65537 - textBytes);
        textBytes += bytes.size();
        require(file.error() == QFile::NoError && textBytes <= 65536 && file.atEnd(),
                "Cannot read SoundFont lists (maximum 64 KiB combined).");
        const auto children = entries(bytes, path.endsWith(".json", Qt::CaseInsensitive));
        active.insert(identity);
        const QDir folder(info.absolutePath());
        for (auto child : children) {
            require(!child.isEmpty(), "SoundFont list contains an empty fileName.");
            child.replace('\\', '/');
            expand(folder.absoluteFilePath(child), depth + 1);
        }
        active.remove(identity);
    }
private:
    QSet<QString> active;
    qint64 textBytes = 0;
};
}

bool isSoundFontList(const QString &path) {
    return path.endsWith(".sflist", Qt::CaseInsensitive) || path.endsWith(".json", Qt::CaseInsensitive);
}
QStringList soundFontPaths(const QString &path) {
    ListReader reader;
    reader.expand(path);
    return reader.paths;
}

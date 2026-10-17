#include "soundfontbank.h"
#include "appsettings.h"
#include "resourceerror.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringDecoder>
#include <stdexcept>

namespace {
void require(bool ok, const QString &message) { if (!ok) throw std::runtime_error(message.toStdString()); }
bool isBank(const QString &name) { return name.endsWith(".sf2", Qt::CaseInsensitive); }
bool isList(const QString &name) { return name.endsWith(".sflist", Qt::CaseInsensitive); }
QStringList bankPaths(const QString &path) {
    if (isBank(path)) return {path};
    require(isList(path), "Select an SF2 bank or a basic SFLIST file.");
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot open SoundFont list: " + QFileInfo(path).fileName());
    require(file.size() <= 65536, "SoundFont list exceeds 64 KiB.");
    const auto bytes = file.read(65537);
    require(file.error() == QFile::NoError && bytes.size() <= 65536 && file.atEnd(), "Cannot read SoundFont list (maximum 64 KiB).");
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(bytes);
    require(!decoder.hasError(), "SoundFont list must be UTF-8 text.");
    QStringList paths; QSet<QString> seen;
    const QDir folder(QFileInfo(path).absolutePath());
    for (auto line : text.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (line.startsWith('"') && line.endsWith('"')) line = line.mid(1, line.size()-2);
        require(line.size() <= 4096 && !line.contains(QChar(0)) && !line.contains('|') && isBank(line),
            "Basic SFLIST supports only SF2 paths, one per line. Nested lists, mappings and JSON are not supported.");
        // Accept Windows-authored relative lists on Unix too.
        line.replace('\\', '/');
        const auto resolved = QDir::cleanPath(folder.absoluteFilePath(line));
        const auto identity = QFileInfo(resolved).canonicalFilePath();
        require(!identity.isEmpty(), "SoundFont list bank is missing: " + line);
        require(!seen.contains(identity), "Duplicate bank in SoundFont list: " + line);
        seen.insert(identity); paths << resolved;
        require(paths.size() <= 32, "SoundFont list exceeds 32 banks.");
    }
    require(!paths.isEmpty(), "SoundFont list contains no banks.");
    return paths;
}
}
QString playerSoundFontsDirectory() {
    const auto test=QCoreApplication::instance()->property("soundFontsDirectoryForTests").toString();
    if (!test.isEmpty()) return test;
    return QDir::cleanPath(QDir(playerThemesDirectory()).absoluteFilePath("../soundfonts"));
}
QStringList availableSoundFonts() {
    QStringList names;
    for (const auto &name:QDir(playerSoundFontsDirectory()).entryList(QDir::Files,QDir::Name))
        if (isBank(name) || isList(name)) names << name;
    return names;
}
QString selectedSoundFont() {
    QSettings s(playerSettingsPath(),playerSettingsFormat()); s.setFallbacksEnabled(false);
    const auto name=s.value("MIDI/SoundFont").toString();
    if (!name.isEmpty() && QFileInfo(name).fileName()==name && !name.contains('/') && !name.contains('\\')) return name;
    return availableSoundFonts().value(0);
}
void selectSoundFont(const QString &name) {
    require(availableSoundFonts().contains(name),"Selected SoundFont is not available.");
    QSettings s(playerSettingsPath(),playerSettingsFormat()); s.setFallbacksEnabled(false);
    s.setValue("MIDI/SoundFont",name); s.sync();
    require(s.status()==QSettings::NoError,"Could not save the selected SoundFont.");
}
QVector<SoundFontBank> loadSoundFontBanks(const QString &name) {
    const auto path = QDir(playerSoundFontsDirectory()).filePath(name);
    if (name.isEmpty() || !QFileInfo::exists(path))
        throw std::runtime_error(resourceRequiredMessage(RequiredResource::SoundFont, "place an .sf2 bank or .sflist in:\n" +
            QDir::toNativeSeparators(playerSoundFontsDirectory())+"\nThen select it in the player's SoundFont list and reopen the MIDI.").toStdString());
    const auto paths = bankPaths(path);
    qint64 total = 0;
    for (const auto &bank : paths) {
        const auto size = QFileInfo(bank).size();
        require(size > 12 && size <= 2LL*1024*1024*1024, "Invalid SoundFont size (maximum 2 GiB per bank).");
        total += size;
    }
    require(total <= 4LL*1024*1024*1024, "SoundFont set exceeds 4 GiB.");
    QVector<SoundFontBank> result; total = 0;
    for (const auto &path : paths) {
        QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot open SoundFont: " + QFileInfo(path).fileName());
        const qint64 expected = file.size();
        // Bound the read even if the file is replaced or grows after the size check.
        require(expected > 12 && expected <= 2LL*1024*1024*1024, "Invalid SoundFont size (maximum 2 GiB per bank).");
        total += expected; require(total <= 4LL*1024*1024*1024, "SoundFont set exceeds 4 GiB.");
        auto bytes = file.read(expected);
        require(file.error() == QFile::NoError && bytes.size() == expected && file.atEnd(), "Cannot read SoundFont.");
        require(bytes.startsWith("RIFF") && bytes.mid(8,4) == "sfbk", "Not an SF2 SoundFont.");
        const auto key = "bitmusic-memory-" + QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex() + ".sf2";
        result.push_back({std::move(bytes), key, QFileInfo(path).fileName()});
    }
    return result;
}

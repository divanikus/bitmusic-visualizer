#include "soundfontbank.h"
#include "soundfontlist.h"
#include "appsettings.h"
#include "resourceerror.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>
#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace {
void require(bool ok, const QString &message) { if (!ok) throw std::runtime_error(message.toStdString()); }
bool isBank(const QString &name) { return name.endsWith(".sf2", Qt::CaseInsensitive); }
QString resourcePath(const QString &name) {
    return QFileInfo(name).isAbsolute() ? name : QDir(playerSoundFontsDirectory()).filePath(name);
}
QString savedSelection() {
    QSettings s(playerSettingsPath(),playerSettingsFormat()); s.setFallbacksEnabled(false);
    return s.value("MIDI/SoundFont").toString();
}
struct BankStamp {
    QString path;
    qint64 size;
    QDateTime modified, changed, created;
    bool operator==(const BankStamp &other) const {
        return path==other.path && size==other.size && modified==other.modified &&
               changed==other.changed && created==other.created;
    }
};
BankStamp stamp(const QFileInfo &info) {
    return {info.canonicalFilePath(), info.size(), info.lastModified(QTimeZone::UTC),
            info.metadataChangeTime(QTimeZone::UTC), info.birthTime(QTimeZone::UTC)};
}
struct CachedBank { BankStamp stamp; SoundFontBank bank; };
struct BankCache {
    std::mutex mutex;
    quint64 revision = 0;
    QVector<CachedBank> banks; // Only the most recently loaded set, not every bank encountered.
};
BankCache &bankCache() { static BankCache cache; return cache; }
}
QString playerSoundFontsDirectory() {
    const auto test=QCoreApplication::instance()->property("soundFontsDirectoryForTests").toString();
    if (!test.isEmpty()) return test;
    return QDir::cleanPath(QDir(playerThemesDirectory()).absoluteFilePath("../soundfonts"));
}
QStringList availableSoundFonts() {
    QStringList names;
    for (const auto &name:QDir(playerSoundFontsDirectory()).entryList(QDir::Files,QDir::Name))
        if (isBank(name) || isSoundFontList(name)) names << name;
    const auto saved = savedSelection();
    if (QFileInfo(saved).isAbsolute() && QFileInfo(saved).isFile() && (isBank(saved) || isSoundFontList(saved)))
        names << saved;
    return names;
}
QString selectedSoundFont() {
    const auto name=savedSelection();
    if (!name.isEmpty() && (QFileInfo(name).isAbsolute() ||
            (QFileInfo(name).fileName()==name && !name.contains('/') && !name.contains('\\')))) return name;
    return availableSoundFonts().value(0);
}
bool selectSoundFont(const QString &name) {
    require(!name.isEmpty(),"Selected SoundFont is not available.");
    const QFileInfo info(resourcePath(name));
    // Validate lists before saving a new choice; no large sample data is read here.
    soundFontPaths(info.absoluteFilePath());
    const auto local = QFileInfo(playerSoundFontsDirectory()).canonicalFilePath();
    const auto path = info.canonicalFilePath();
    const auto stored = !local.isEmpty() && QFileInfo(path).absolutePath() == local ? QFileInfo(path).fileName() : path;
    const bool changed = QFileInfo(resourcePath(selectedSoundFont())).canonicalFilePath() != path;
    QSettings s(playerSettingsPath(),playerSettingsFormat()); s.setFallbacksEnabled(false);
    s.setValue("MIDI/SoundFont",stored); s.sync();
    require(s.status()==QSettings::NoError,"Could not save the selected SoundFont.");
    return changed;
}
void clearSoundFontBankCache() {
    auto &cache=bankCache();
    std::lock_guard lock(cache.mutex);
    ++cache.revision;
    cache.banks.clear();
}
QVector<SoundFontBank> loadSoundFontBanks(const QString &name) {
    const auto path = resourcePath(name);
    if (name.isEmpty() || !QFileInfo::exists(path))
        throw std::runtime_error(resourceRequiredMessage(RequiredResource::SoundFont,
            "choose an SF2 bank or SFLIST/JSON set with Browse... in the player's SoundFont list.\n"
            "You can also place it in:\n" + QDir::toNativeSeparators(playerSoundFontsDirectory()) +
            (name.isEmpty() ? QString() : "\nMissing selection: " + name)).toStdString());
    const auto paths = soundFontPaths(path);
    qint64 total = 0;
    for (const auto &bank : paths) {
        const auto size = QFileInfo(bank).size();
        require(size > 12 && size <= 2LL*1024*1024*1024, "Invalid SoundFont size (maximum 2 GiB per bank).");
        total += size;
    }
    require(total <= 4LL*1024*1024*1024, "SoundFont set exceeds 4 GiB.");
    auto &cache=bankCache();
    QVector<CachedBank> previous, next;
    quint64 revision;
    {
        std::lock_guard lock(cache.mutex);
        previous=cache.banks; revision=cache.revision;
    }
    QVector<SoundFontBank> result; total = 0;
    for (const auto &path : paths) {
        QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot open SoundFont: " + QFileInfo(path).fileName());
        const qint64 expected = file.size();
        // Bound the read even if the file is replaced or grows after the size check.
        require(expected > 12 && expected <= 2LL*1024*1024*1024, "Invalid SoundFont size (maximum 2 GiB per bank).");
        total += expected; require(total <= 4LL*1024*1024*1024, "SoundFont set exceeds 4 GiB.");
        const auto identity=stamp(QFileInfo(file));
        const auto cached=std::find_if(previous.cbegin(),previous.cend(),[&](const CachedBank &entry) {
            return !identity.path.isEmpty() && identity.modified.isValid() && entry.stamp==identity;
        });
        if(cached!=previous.cend()) {
            auto bank=cached->bank;
            bank.name=QFileInfo(path).fileName();
            result.push_back(bank); next.push_back({identity,std::move(bank)});
            continue;
        }
        auto bytes = file.read(expected);
        require(file.error() == QFile::NoError && bytes.size() == expected && file.atEnd(), "Cannot read SoundFont.");
        require(bytes.startsWith("RIFF") && bytes.mid(8,4) == "sfbk", "Not an SF2 SoundFont.");
        const auto key = "bitmusic-memory-" + QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex() + ".sf2";
        require(identity==stamp(QFileInfo(path)), "SoundFont changed while loading. Please retry.");
        SoundFontBank bank{std::move(bytes),key,QFileInfo(path).fileName()};
        result.push_back(bank); next.push_back({identity,std::move(bank)});
    }
    {
        std::lock_guard lock(cache.mutex);
        // Refresh must also invalidate a read that was already in flight.
        if(cache.revision==revision) cache.banks=std::move(next);
    }
    return result;
}

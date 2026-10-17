#pragma once
#include <QByteArray>
#include <QStringList>
#include <QVector>

struct SoundFontBank {
    QByteArray bytes, key; // Content identity also keys FluidSynth's shared sample cache.
    QString name;
};
QString playerSoundFontsDirectory();
QStringList availableSoundFonts();
QString selectedSoundFont();
void selectSoundFont(const QString &name);
// Forget retained input bytes; existing decoder snapshots remain valid.
void clearSoundFontBankCache();
// Basic UTF-8 SFLIST: one SF2 path per line; paths are relative to the list.
// Later banks take priority when the same bank/program is present more than once.
// Reuses the last set while each file's path, size and timestamps are unchanged.
QVector<SoundFontBank> loadSoundFontBanks(const QString &name);

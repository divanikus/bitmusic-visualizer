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
// Accept local filenames or absolute external paths. Returns whether the choice changed.
bool selectSoundFont(const QString &name);
// Forget retained input bytes; existing decoder snapshots remain valid.
void clearSoundFontBankCache();
// UTF-8 text/JSON SFLIST, including nested lists; paths are relative to each list.
// Later banks take priority when the same bank/program is present more than once.
// Reuses the last set while each file's path, size and timestamps are unchanged.
QVector<SoundFontBank> loadSoundFontBanks(const QString &name);

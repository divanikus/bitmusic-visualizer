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
// Basic UTF-8 SFLIST: one SF2 path per line; paths are relative to the list.
// Later banks take priority when the same bank/program is present more than once.
QVector<SoundFontBank> loadSoundFontBanks(const QString &name);

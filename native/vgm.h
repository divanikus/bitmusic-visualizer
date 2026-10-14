#pragma once
#include <QByteArray>
#include <QStringList>

// Header fields end before command data AND before any VGM extra header.
struct VgmHeader {
    quint32 version = 0, pokeyClock = 0;
    qsizetype dataOffset = 0, headerEnd = 0;
    QStringList unsupported;
    bool sega = false;
};
VgmHeader inspectVgm(const QByteArray &data);
void requireSupportedVgm(const VgmHeader &header);

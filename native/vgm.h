#pragma once
#include <QByteArray>
#include <QStringList>
#include <QVector>

struct VgmYamahaChip {
    int id = 0, instance = 0;
    quint32 clock = 0;
    QString name;
    bool stereo = false;
    int pan = 0; // -1/+1 for VGM dual-OPL2 hard panning, otherwise native routing.
    double gain = 1, ssgGain = 1;
    bool variant = false; // YM2610B; separate from dual-OPL2 output panning.
};

// Header fields end before command data AND before any VGM extra header.
struct VgmHeader {
    quint32 version = 0, pokeyClock = 0;
    qsizetype dataOffset = 0, headerEnd = 0;
    QStringList unsupported;
    QVector<VgmYamahaChip> yamaha;
    bool sega = false;
};
VgmHeader inspectVgm(const QByteArray &data);
void requireSupportedVgm(const VgmHeader &header);

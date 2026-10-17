#pragma once
#include <QByteArray>
#include <QStringList>

struct SoundConfiguration {
    QString id, name, family;
    QStringList sources; // Stable hardware roles in the backend's voice order.
};
SoundConfiguration soundConfiguration(const QByteArray &decodedFile, const QStringList &voices);

struct VgmHeader;
// The decoder has already inspected this same immutable decoded VGM data.
SoundConfiguration vgmSoundConfiguration(const QByteArray &decodedFile, const QStringList &voices, const VgmHeader &header);

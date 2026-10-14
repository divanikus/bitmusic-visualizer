#pragma once
#include <QByteArray>
#include <QStringList>

struct SoundConfiguration {
    QString id, name, family;
    QStringList sources; // Stable hardware roles in the backend's voice order.
};
SoundConfiguration soundConfiguration(const QByteArray &decodedFile, const QStringList &voices);

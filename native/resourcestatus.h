#pragma once
#include <QString>
#include <QVector>

// Presence/readability/size only. Decoders validate the actual resource contents.
struct ResourceStatus {
    QString file, path, status, detail;
    bool found = false;
    bool directory = false;
};
QVector<ResourceStatus> inspectPlayerResources();

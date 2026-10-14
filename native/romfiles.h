#pragma once
#include <QByteArray>
#include <QString>
#include <array>
#include <memory>

// One immutable snapshot for a loaded tune, shared by audio and scope decoders.
struct SidRoms {
    std::array<QByteArray, 3> bytes;
    std::array<QString, 3> problem;
    QString directory;
};
QString playerRomsDirectory();
std::shared_ptr<const SidRoms> loadSidRoms();
[[noreturn]] void missingSidRom(const SidRoms &roms, int index);

struct YamahaRoms {
    QByteArray rhythm;
    QString problem, path;
};
std::shared_ptr<const YamahaRoms> loadYamahaRoms();
[[noreturn]] void missingYamahaRom(const YamahaRoms &roms);

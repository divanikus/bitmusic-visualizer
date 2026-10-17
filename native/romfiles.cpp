#include "romfiles.h"
#include "appsettings.h"
#include "resourceerror.h"
#include <QFile>
#include <stdexcept>

namespace {
const std::array<const char *, 3> names{"kernal.rom", "basic.rom", "chargen.rom"};
const std::array<int, 3> sizes{8192, 8192, 4096};
struct RomRead { QByteArray bytes; QString problem; };
RomRead readFixedRom(const QString &path, int size) {
    QFile file(path);
    if (!file.exists()) return {{}, "File is missing."};
    if (!file.open(QIODevice::ReadOnly)) return {{}, "File cannot be read."};
    const auto bytes = file.read(size + 1);
    if (bytes.size() != size || file.error() != QFile::NoError)
        return {{}, QString("Expected exactly %1 bytes.").arg(size)};
    return {bytes, {}};
}
}
QString playerRomsDirectory() {
    const auto testPath = QCoreApplication::instance()->property("romsDirectoryForTests").toString();
    if (!testPath.isEmpty()) return testPath;
    return QDir::cleanPath(QDir(playerThemesDirectory()).absoluteFilePath("../roms"));
}
std::shared_ptr<const SidRoms> loadSidRoms() {
    auto result = std::make_shared<SidRoms>();
    result->directory = QDir(playerRomsDirectory()).filePath("c64");
    for (int i = 0; i < 3; ++i) {
        const auto rom = readFixedRom(QDir(result->directory).filePath(names[i]), sizes[i]);
        result->bytes[i] = rom.bytes;
        result->problem[i] = rom.problem;
    }
    return result;
}
void missingSidRom(const SidRoms &roms, int index) {
    throw std::runtime_error(resourceRequiredMessage(RequiredResource::C64Rom,
        QString("%1\n%2\nPlace your %3-byte ROM at:\n%4\nThen reopen the music file.")
        .arg(names[index], roms.problem[index]).arg(sizes[index])
        .arg(QDir::toNativeSeparators(QDir(roms.directory).filePath(names[index])))).toStdString());
}

std::shared_ptr<const YamahaRoms> loadYamahaRoms() {
    auto result = std::make_shared<YamahaRoms>();
    result->path = QDir(playerRomsDirectory()).filePath("ymfm/ym2608_adpcm_rom.bin");
    const auto rom = readFixedRom(result->path, 8192);
    result->rhythm = rom.bytes;
    result->problem = rom.problem;
    return result;
}
void missingYamahaRom(const YamahaRoms &roms) {
    throw std::runtime_error(resourceRequiredMessage(RequiredResource::Ym2608Rom,
        QString("ym2608_adpcm_rom.bin\n%1\n"
        "This track uses the chip's rhythm samples. Place your 8192-byte ROM at:\n%2\nThen reopen the music file.")
        .arg(roms.problem, QDir::toNativeSeparators(roms.path))).toStdString());
}

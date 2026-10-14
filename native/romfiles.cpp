#include "romfiles.h"
#include "appsettings.h"
#include <QFile>
#include <stdexcept>

namespace {
const std::array<const char *, 3> names{"kernal.rom", "basic.rom", "chargen.rom"};
const std::array<int, 3> sizes{8192, 8192, 4096};
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
        QFile file(QDir(result->directory).filePath(names[i]));
        if (!file.exists()) result->problem[i] = "File is missing.";
        else if (!file.open(QIODevice::ReadOnly)) result->problem[i] = "File cannot be read.";
        else {
            auto bytes = file.read(sizes[i] + 1);
            if (bytes.size() == sizes[i] && file.error() == QFile::NoError) result->bytes[i] = bytes;
            else result->problem[i] = QString("Expected exactly %1 bytes.").arg(sizes[i]);
        }
    }
    return result;
}
void missingSidRom(const SidRoms &roms, int index) {
    throw std::runtime_error(QString("C64 ROM required: %1\n%2\nPlace your %3-byte ROM at:\n%4\nThen reopen the music file.")
        .arg(names[index], roms.problem[index]).arg(sizes[index])
        .arg(QDir::toNativeSeparators(QDir(roms.directory).filePath(names[index]))).toStdString());
}

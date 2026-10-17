#include "resourcestatus.h"
#include "romfiles.h"
#include "miditrack.h"
#include "mt32track.h"
#include <QDir>
#include <QFile>

namespace {
ResourceStatus inspectFile(const QString &name, const QString &path, qint64 size = 0) {
    ResourceStatus row{name, path, "Missing", "Place this file in the folder shown below."};
    QFile file(path);
    if (!file.exists()) return row;
    if (!file.open(QIODevice::ReadOnly)) { row.status = "Unreadable"; row.detail = file.errorString(); return row; }
    if (file.size() <= 0 || (size && file.size() != size) || (!size && file.size() > 2 * 1024 * 1024)) {
        row.status = "Invalid size";
        row.detail = size ? QString("Expected %1 bytes.").arg(size) : "Expected a complete Control or PCM ROM image.";
        return row;
    }
    row.status = size ? "Found (size OK)" : "Found"; row.found = true;
    row.detail = size ? "File size matches. Contents are not verified by this check." :
        "Munt checks ROM identity and matching Control/PCM models when opening music.";
    return row;
}
}
QVector<ResourceStatus> inspectPlayerResources() {
    const QDir roms(playerRomsDirectory());
    QVector<ResourceStatus> rows;
    for (const auto &name : {"MT32_CONTROL.ROM", "MT32_PCM.ROM"})
        rows << inspectFile(name, QDir(playerMt32Directory()).filePath(name));
    rows << inspectFile("kernal.rom", roms.filePath("c64/kernal.rom"), 8192)
         << inspectFile("basic.rom", roms.filePath("c64/basic.rom"), 8192)
         << inspectFile("chargen.rom", roms.filePath("c64/chargen.rom"), 4096)
         << inspectFile("ym2608_adpcm_rom.bin", roms.filePath("ymfm/ym2608_adpcm_rom.bin"), 8192);
    const auto banks = availableSoundFonts();
    rows << ResourceStatus{"SoundFonts (*.sf2, *.sflist)", playerSoundFontsDirectory(),
        banks.isEmpty() ? "Missing" : QString("%1 found").arg(banks.size()),
        banks.isEmpty() ? "General MIDI needs a user-supplied SF2 bank." :
            "Choose a bank in the player when MIDI is open. Banks are validated during playback setup.",
        !banks.isEmpty(), true};
    return rows;
}

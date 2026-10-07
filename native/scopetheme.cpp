#include "scopetheme.h"
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>

namespace {
const std::array<const char *, 5> keys = {"Waveform", "Label", "Border", "Background", "Axis"};
const std::array<QColor ScopeColors::*, 5> members = {
    &ScopeColors::waveform, &ScopeColors::label, &ScopeColors::border, &ScopeColors::background, &ScopeColors::axis
};
QString tileGroup(int slot) { return QString("Tile%1").arg(slot + 1, 2, 10, QLatin1Char('0')); }
}

ScopeColors ScopeColors::defaults(int channel) {
    const QColor palette[] = {QColor("#75e2cb"), QColor("#8fb8ff"), QColor("#c5abff"), QColor("#ecc38a")};
    return {palette[channel % 4], palette[channel % 4], QColor("#314047"), QColor("#1b252b"), QColor("#293e48")};
}

ScopeTheme::ScopeTheme() {
    for (int i = 0; i < SlotCount; ++i) tiles[i] = ScopeColors::defaults(i);
}

bool ScopeTheme::load(const QString &path, QString &error) {
    error.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 65536) { error = "Theme file is too large (maximum 64 KiB)."; return false; }
    file.close();
    QSettings ini(path, QSettings::IniFormat); ini.setFallbacksEnabled(false);
    if (ini.value("Theme/Version").toString() != "1" || ini.value("Theme/Slots").toString() != QString::number(SlotCount)) {
        error = "Expected theme Version=1 and Slots=32."; return false;
    }
    const QRegularExpression rgb("^#[0-9A-Fa-f]{6}$");
    auto read = [&](const QString &key, QColor &color) {
        const auto value = ini.value(key).toString();
        if (!rgb.match(value).hasMatch()) { error = "Missing or invalid color: " + key + ". Use #RRGGBB."; return false; }
        color = QColor(value); return true;
    };
    ScopeTheme candidate;
    if (!read("Theme/WindowBackground", candidate.windowBackground)) return false;
    for (int slot = 0; slot < SlotCount; ++slot)
        for (size_t field = 0; field < keys.size(); ++field)
            if (!read(tileGroup(slot) + '/' + keys[field], candidate.tiles[slot].*members[field])) return false;
    // Optional extension: existing 32-slot themes still load, preserving their
    // former output color until the user gives Full mix its own palette.
    candidate.fullMix = candidate.tiles[0];
    if (ini.childGroups().contains("FullMix"))
        for (size_t field = 0; field < keys.size(); ++field)
            if (!read(QString("FullMix/") + keys[field], candidate.fullMix.*members[field])) return false;
    if (ini.status() != QSettings::NoError) { error = "Could not read the INI theme."; return false; }
    *this = candidate; return true;
}

bool ScopeTheme::save(const QString &path, QString &error) const {
    error.clear();
    QByteArray data("; Bit Music Visualizer scope theme. Tile numbers follow the original channel order.\n[Theme]\nVersion=1\nSlots=32\nWindowBackground=");
    data += windowBackground.name().toUpper().toUtf8() + '\n';
    for (int slot = 0; slot < SlotCount; ++slot) {
        data += "\n[" + tileGroup(slot).toUtf8() + "]\n";
        for (size_t field = 0; field < keys.size(); ++field)
            data += QByteArray(keys[field]) + '=' + (tiles[slot].*members[field]).name().toUpper().toUtf8() + '\n';
    }
    data += "\n[FullMix]\n";
    for (size_t field = 0; field < keys.size(); ++field)
        data += QByteArray(keys[field]) + '=' + (fullMix.*members[field]).name().toUpper().toUtf8() + '\n';
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = file.errorString(); return false;
    }
    return true;
}

#include "voicemask.h"
#include "scopepreset.h"
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>

QByteArray ScopePreset::encode() const {
    QByteArray data("; Bit Music Visualizer layout preset\n[Preset]\nVersion=1\n");
    auto put = [&](const char *key, const QString &value) { data += key; data += '=' + value.toUtf8() + '\n'; };
    auto num = [&](const char *key, int value) { put(key, QString::number(value)); };
    put("Configuration", configuration.id); put("Family", configuration.family);
    // Configuration names are display-only; fixed internal labels contain no INI metacharacters.
    put("ConfigurationName", configuration.name); put("Sources", configuration.sources.join('|'));
    num("Rows", rows); num("Columns", columns); num("Numbers", numbers); num("Names", names); num("Stereo", stereo);
    num("Cards", int(cards.size()));
    for (int i = 0; i < cards.size(); ++i) {
        const auto &card = cards[i]; data += "\n[Card" + QByteArray::number(i+1) + "]\n";
        put("Source", card.source); num("View", card.view); num("Width", card.span.width()); num("Height", card.span.height());
        num("Visible", card.visible); num("Bars", card.bars); num("Grid", card.grid);
    }
    return data;
}
bool ScopePreset::load(const QString &path, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 131072) { error = "Preset exceeds 128 KiB."; return false; }
    file.close(); QSettings ini(path, QSettings::IniFormat); ini.setFallbacksEnabled(false);
    ScopePreset candidate; bool valid = true;
    auto integer = [&](const QString &key, int low, int high) {
        bool ok = false; const int value = ini.value(key).toString().toInt(&ok);
        if (!ok || value < low || value > high) valid = false;
        return value;
    };
    integer("Preset/Version", 1, 1);
    auto &c = candidate.configuration;
    c.id = ini.value("Preset/Configuration").toString(); c.family = ini.value("Preset/Family").toString();
    c.name = ini.value("Preset/ConfigurationName").toString();
    c.sources = ini.value("Preset/Sources").toString().split('|');
    const QRegularExpression token("^[a-z0-9.:-]{1,90}$");
    valid &= QRegularExpression("^[a-f0-9]{64}$").match(c.id).hasMatch() && token.match(c.family).hasMatch();
    valid &= !c.name.isEmpty() && c.name.size() <= 160 && !c.name.contains(QRegularExpression("[\\r\\n=;\\[\\]]"));
    QSet<QString> sources;
    for (const auto &source : c.sources) { valid &= token.match(source).hasMatch() && source != "mix" && !sources.contains(source); sources.insert(source); }
    valid &= c.sources.size() >= 1 && c.sources.size() <= VoiceMask::Capacity;
    candidate.rows = integer("Preset/Rows", 1, 16); candidate.columns = integer("Preset/Columns", 1, 8);
    candidate.numbers = integer("Preset/Numbers", 0, 1); candidate.names = integer("Preset/Names", 0, 1);
    candidate.stereo = integer("Preset/Stereo", 0, 1);
    const int count = integer("Preset/Cards", 0, 512);
    if (valid) for (int i = 0; i < count; ++i) {
        const auto group = QString("Card%1/").arg(i+1);
        PresetCard card; card.source = ini.value(group+"Source").toString();
        valid &= card.source == "mix" || sources.contains(card.source);
        card.view = integer(group+"View", 0, 2);
        card.span = QSize(integer(group+"Width", 1, candidate.columns), integer(group+"Height", 1, candidate.rows));
        card.visible = integer(group+"Visible", 0, 1); card.bars = integer(group+"Bars", 0, 1); card.grid = integer(group+"Grid", 0, 1);
        candidate.cards.push_back(card);
    }
    if (!valid || ini.status() != QSettings::NoError) { error = "Invalid or unsupported layout preset."; return false; }
    *this = candidate; error.clear(); return true;
}
bool ScopePreset::save(const QString &path, QString &error) const {
    const auto data = encode(); QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) { error = file.errorString(); return false; }
    error.clear(); return true;
}
bool ScopePreset::compatible(const SoundConfiguration &target) const {
    if (configuration.id == target.id) return configuration.family == target.family && configuration.sources == target.sources;
    return !target.family.isEmpty() && configuration.family == target.family;
}
ScopePreset ScopePreset::adapted(const SoundConfiguration &target) const {
    ScopePreset result = *this; result.configuration = target; result.cards.clear();
    for (const auto &card : cards) if (card.source == "mix" || target.sources.contains(card.source)) result.cards.push_back(card);
    // Preserve intentionally removed cards. Only hardware absent from the old configuration is new.
    for (const auto &source : target.sources) if (!configuration.sources.contains(source))
        result.cards.push_back({source});
    return result;
}

#pragma once
#include <QColor>
#include <QString>
#include <array>

struct ScopeColors {
    QColor waveform, label, border, background, axis;
    static ScopeColors defaults(int channel);
};

struct ScopeTheme {
    static constexpr int SlotCount = 32;
    static constexpr int FullMix = SlotCount; // Display-only ID; never a voice-mask bit.
    std::array<ScopeColors, SlotCount> tiles;
    ScopeColors fullMix = ScopeColors::defaults(0);
    ScopeColors &colors(int id) { return id == FullMix ? fullMix : tiles.at(id); }
    const ScopeColors &colors(int id) const { return id == FullMix ? fullMix : tiles.at(id); }
    QColor windowBackground{"#171e24"};
    ScopeTheme();
    // Failed reads leave the current theme intact; writes replace files atomically.
    bool load(const QString &path, QString &error);
    bool save(const QString &path, QString &error) const;
};

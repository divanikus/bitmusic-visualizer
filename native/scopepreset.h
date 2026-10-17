#pragma once
#include "soundconfig.h"
#include <QSize>
#include <QVector>

struct PresetCard {
    QString source;
    int view = 0;
    QSize span{1, 1};
    bool visible = true, bars = false, grid = true;
    bool horizontal = false, commands = false;
    int firstChannel = 0;
    int keyboardSize = 25;
};
struct ScopePreset {
    static constexpr int MaximumCards = 512;
    SoundConfiguration configuration;
    int rows = 5, columns = 1;
    bool numbers = true, names = true, stereo = false;
    QVector<PresetCard> cards;
    QByteArray encode() const;
    bool load(const QString &path, QString &error);
    bool save(const QString &path, QString &error) const;
    bool compatible(const SoundConfiguration &target) const;
    ScopePreset adapted(const SoundConfiguration &target) const;
};

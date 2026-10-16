// SPDX-License-Identifier: GPL-2.0-or-later
// TIATracker replay adaptation; see README.md in this directory.
#pragma once
#include <QByteArray>
#include <QStringList>
#include <QVector>
#include <array>

namespace Ttt {
struct Tick {
    // AUDC, AUDF, AUDV for each of the two hardware voices.
    std::array<std::array<int,3>,2> registers{};
    std::array<int,2> instruments{{-1,-1}};
};
struct Song {
    QString title;
    QStringList instruments;
    QVector<Tick> ticks;
    int rate=50, loop=-1;
};
bool recognizes(const QByteArray &data);
Song compile(const QByteArray &data);
}

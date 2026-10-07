#pragma once
#include <QHash>
#include <QRect>
#include <QSize>
#include <QVector>

using ScopeGrid = QHash<int, QRect>;

// Stable channel IDs, cell coordinates. Later small cards can fill holes left
// by a larger overflow card. This has no audio or pixel-coordinate dependency.
ScopeGrid packScopes(int rows, int columns, const QVector<int> &order,
                     const QVector<bool> &hidden, const QVector<QSize> &spans);

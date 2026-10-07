#include "scopegrid.h"
#include <algorithm>

ScopeGrid packScopes(int rows, int columns, const QVector<int> &order,
                     const QVector<bool> &hidden, const QVector<QSize> &spans) {
    ScopeGrid result;
    if (rows < 1 || columns < 1 || rows > 16 || columns > 8) return result;
    QVector<bool> used(rows * columns, false);
    for (int id : order) {
        if (id < 0 || id >= hidden.size() || id >= spans.size() || hidden[id] || result.contains(id)) continue;
        const int w = std::clamp(spans[id].width(), 1, columns), h = std::clamp(spans[id].height(), 1, rows);
        bool placed = false;
        for (int y = 0; y <= rows-h && !placed; ++y) for (int x = 0; x <= columns-w; ++x) {
            bool free = true;
            for (int dy = 0; dy < h && free; ++dy) for (int dx = 0; dx < w; ++dx)
                if (used[(y+dy)*columns+x+dx]) { free = false; break; }
            if (!free) continue;
            result.insert(id, QRect(x, y, w, h));
            for (int dy = 0; dy < h; ++dy) for (int dx = 0; dx < w; ++dx) used[(y+dy)*columns+x+dx] = true;
            placed = true; break;
        }
    }
    return result;
}

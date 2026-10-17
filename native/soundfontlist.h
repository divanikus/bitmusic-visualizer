#pragma once
#include <QStringList>

bool isSoundFontList(const QString &path);
// Expand UTF-8 text/JSON lists relative to each containing file. Later banks win.
// Only SF2 paths are supported; BASSMIDI mapping/gain/channel rules are rejected.
QStringList soundFontPaths(const QString &path);

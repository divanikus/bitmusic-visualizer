#pragma once
#include <QKeySequence>

inline QString shortcutLabel(Qt::KeyboardModifiers modifiers, Qt::Key key) {
    return QKeySequence(QKeyCombination(modifiers, key)).toString(QKeySequence::NativeText);
}
inline QString channelShortcutHint(int source) {
    const auto key = Qt::Key(Qt::Key_1 + source);
    return QString("%1: cycle view; %2: mute/unmute; %3: show/hide.\nF1: all shortcuts.")
        .arg(source+1).arg(shortcutLabel(Qt::ControlModifier, key), shortcutLabel(Qt::AltModifier, key));
}

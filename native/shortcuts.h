#pragma once
#include <QKeySequence>

class QWidget;
class QPushButton;
QPushButton *createShortcutsHelpButton(QWidget *owner);
void showShortcutsHelp(QWidget *owner);

inline QString shortcutLabel(Qt::KeyboardModifiers modifiers, Qt::Key key) {
    return QKeySequence(QKeyCombination(modifiers, key)).toString(QKeySequence::NativeText);
}
inline QString channelShortcutHint(int source) {
    const auto key = Qt::Key(Qt::Key_1 + source);
    return QString("%1: cycle view; %2: mute/unmute; %3: show/hide.\n%4: Solo / restore. F1: all shortcuts.")
        .arg(source+1).arg(shortcutLabel(Qt::ControlModifier, key), shortcutLabel(Qt::AltModifier, key),
            shortcutLabel(Qt::ControlModifier | Qt::ShiftModifier, key));
}
inline QString fullMixShortcutHint() {
    return QString("0: cycle view; %1: show/hide.\nF1: all shortcuts.")
        .arg(shortcutLabel(Qt::AltModifier, Qt::Key_0));
}
inline QString stereoShortcutHint() {
    return QString("Stereo (%1)\nShow left and right lanes with the same time and amplitude scale")
        .arg(shortcutLabel(Qt::ShiftModifier, Qt::Key_S));
}

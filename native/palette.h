#pragma once
#include <QPalette>
#include <QComboBox>
#include <QAbstractItemView>

// The player has dark chrome regardless of the operating system's theme.
inline QPalette playerPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#1b252b")); palette.setColor(QPalette::WindowText, QColor("#dce5e8"));
    palette.setColor(QPalette::Base, QColor("#202e35")); palette.setColor(QPalette::AlternateBase, QColor("#293b41"));
    palette.setColor(QPalette::Text, QColor("#dce5e8")); palette.setColor(QPalette::PlaceholderText, QColor("#92a6b0"));
    palette.setColor(QPalette::Button, QColor("#293b41")); palette.setColor(QPalette::ButtonText, QColor("#dce5e8"));
    palette.setColor(QPalette::Highlight, QColor("#75e2cb")); palette.setColor(QPalette::HighlightedText, QColor("#133c38"));
    palette.setColor(QPalette::ToolTipBase, QColor("#304048")); palette.setColor(QPalette::ToolTipText, QColor("#ecf2f3"));
    palette.setColor(QPalette::Light, QColor("#465b62")); palette.setColor(QPalette::Midlight, QColor("#354950"));
    palette.setColor(QPalette::Dark, QColor("#11191e")); palette.setColor(QPalette::Mid, QColor("#293b41"));
    palette.setColor(QPalette::Shadow, QColor("#0b1115"));
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor("#81969f"));
    return palette;
}

inline void styleCombo(QComboBox *combo) {
    combo->setPalette(playerPalette());
    combo->setStyleSheet("QComboBox { combobox-popup: 0; }");
    combo->setMaxVisibleItems(12);
    combo->view()->setPalette(playerPalette());
    combo->view()->setStyleSheet(R"(
        QAbstractItemView { background: #202e35; color: #dce5e8;
            selection-background-color: #75e2cb; selection-color: #133c38;
            border: 1px solid #465b62; outline: none; }
        QAbstractItemView::item { padding: 5px; }
        QAbstractItemView::item:selected { background: #75e2cb; color: #133c38; }
    )");
}

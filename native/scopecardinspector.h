#pragma once
#include "scopecards.h"
#include <QScrollArea>
#include <functional>

class QLabel;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QPushButton;

// Displays a selection without retaining a pointer into the card model. Setting
// the selection never emits edits; user changes are applied by the window.
class ScopeCardInspector : public QScrollArea {
public:
    explicit ScopeCardInspector(QWidget *parent = nullptr, bool colorsButton = true);
    void setCard(const ScopeCard *card, const QString &name, ScopeViewSupport support,
                 const QString &keyboardHint, int sources);
    QSize sizeHint() const override;
    std::function<void(ScopeView)> onViewChanged;
    std::function<void(bool bars, bool grid)> onSpectrumChanged;
    std::function<void(const ScopeCard &options)> onMusicChanged;
    std::function<void()> onColorsRequested;
    std::function<void(bool)> onLevelsChanged;
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    QLabel *selectedCard_, *keyboardHint_;
    QComboBox *cardView_, *spectrumStyle_, *rollDirection_;
    QWidget *spectrumControls_, *musicControls_;
    QCheckBox *spectrumGrid_, *patternCommands_, *levels_;
    QSpinBox *rollKeyboardSize_, *patternChannel_;
    QPushButton *viewHelp_;
};

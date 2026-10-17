#include "playerwindow.h"
#include "appsettings.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr,"Card shortcut check: %s\n",message); throw std::runtime_error(message); }
}
void pump(QApplication &app, int ms = 30) {
    QEventLoop loop(&app); QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
void until(QApplication &app, const std::function<bool()> &ready) {
    QElapsedTimer time; time.start();
    while (!ready() && time.elapsed() < 8000) pump(app);
    require(ready(), "Selected-card shortcut check timed out.");
}
void key(QWidget &target, int key, Qt::KeyboardModifiers mods = {}, bool repeat = false) {
    const QString text = key >= Qt::Key_A && key <= Qt::Key_Z ? QString(QChar(key)).toLower() : QString();
    QKeyEvent claim(QEvent::ShortcutOverride,key,mods,text,repeat);
    QApplication::sendEvent(&target,&claim);
    QKeyEvent press(QEvent::KeyPress,key,mods,text,repeat); QApplication::sendEvent(&target,&press);
    QKeyEvent release(QEvent::KeyRelease,key,mods,text); QApplication::sendEvent(&target,&release);
}
void mouse(QWidget &scope, QEvent::Type type, QPointF point) {
    QMouseEvent event(type,point,scope.mapToGlobal(point.toPoint()),type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&scope,&event);
}
void checkCardMovement(QApplication &app, PlayerWindow &window, QTextStream &log) {
    auto &scope=window.scopeWindow(); auto &player=window.player();
    const auto state=player.state();
    scope.show(); scope.resize(1250,850); scope.activateWindow(); pump(app);
    auto edit=scope.findChild<QPushButton *>("editLayout");
    auto rows=scope.findChild<QSpinBox *>("gridRows"), columns=scope.findChild<QSpinBox *>("gridColumns");
    auto list=scope.findChild<QListWidget *>("scopeChannelList");
    const auto order=[&] {
        QVector<int> ids;
        for (int i=0;i<list->count();++i) ids.push_back(list->item(i)->data(Qt::UserRole).toInt());
        return ids;
    };
    const auto select=[&](int id) { list->setCurrentRow(order().indexOf(id)); scope.setFocus(); };
    const auto selected=[&] { return list->currentItem()->data(Qt::UserRole).toInt(); };
    const auto rect=[&](int id) { pump(app,180); return scope.panelRect(id); };
    const auto move=[&](int keyCode, bool repeat=false) { key(scope,keyCode,Qt::AltModifier,repeat); };
    scope.resetLayout(); rows->setValue(3); columns->setValue(3); edit->setChecked(false); scope.setFocus();
    const auto original=order();
    move(Qt::Key_Right); require(order()==original,"Alt+arrow moved without selection.");
    select(4); move(Qt::Key_Left); require(order().indexOf(4)==3 && selected()==4,"Move Left lost card identity.");
    move(Qt::Key_Right); move(Qt::Key_Up); require(order().indexOf(4)==1,"Move Up did not follow grid columns.");
    move(Qt::Key_Down); require(order()==original,"Directional moves failed to restore the simple grid.");
    move(Qt::Key_Right); const auto edge=order(); move(Qt::Key_Right,true);
    require(order()==edge,"Move wrapped at the right grid edge.");
    move(Qt::Key_Down); const auto bottom=order(); move(Qt::Key_Down);
    require(order()==bottom && selected()==4,"Move crossed the bottom grid edge.");
    select(0); move(Qt::Key_Right); move(Qt::Key_Right,true);
    require(order().indexOf(0)==2,"Held movement did not repeat.");
    // No action on override events, conflicting modifiers, fields or another window.
    const auto protectedOrder=order();
    QKeyEvent claim(QEvent::ShortcutOverride,Qt::Key_Left,Qt::AltModifier); claim.setAccepted(false);
    QApplication::sendEvent(&scope,&claim);
    require(claim.isAccepted() && order()==protectedOrder,"Move shortcut override executed an action.");
    key(scope,Qt::Key_Left,Qt::AltModifier|Qt::ShiftModifier);
    key(window,Qt::Key_Left,Qt::AltModifier);
    edit->setChecked(true); rows->findChild<QLineEdit *>()->setFocus(); move(Qt::Key_Left);
    require(order()==protectedOrder,"Movement escaped modifiers, window or input focus guards.");
    // A large overflow card may refill the grid after reordering; never lose the selected card.
    scope.resetLayout(); rows->setValue(3); columns->setValue(3); select(2); key(scope,Qt::Key_H);
    key(scope,Qt::Key_Right,Qt::ShiftModifier);
    for (int i=0;i<2;++i) key(scope,Qt::Key_Down,Qt::ShiftModifier);
    key(scope,Qt::Key_H); select(0); const auto beforeOverflow=order(); move(Qt::Key_Down);
    require(order()==beforeOverflow && scope.visibleChannels().contains(0),"Move lost the selected card to overflow.");
    // Compact packing cannot always honor a direction for differently sized cards.
    scope.resetLayout(); rows->setValue(3); columns->setValue(3);
    for (int i=3;i<16;++i) list->item(i)->setCheckState(Qt::Unchecked);
    select(1); key(scope,Qt::Key_Right,Qt::ShiftModifier); key(scope,Qt::Key_Down,Qt::ShiftModifier);
    select(2); key(scope,Qt::Key_Right,Qt::ShiftModifier);
    select(0); const auto beforeWrongDirection=order(); move(Qt::Key_Down);
    require(order()==beforeWrongDirection,"Repacking moved the selected card against the requested direction.");
    // Hidden/overflow entries move one list position until they have a grid position.
    scope.resetLayout(); rows->setValue(2); columns->setValue(2);
    select(1); key(scope,Qt::Key_H); list->setFocus();
    key(*list,Qt::Key_Up,Qt::AltModifier);
    require(order().front()==1 && list->currentItem()->checkState()==Qt::Unchecked,"Moving a hidden card changed visibility.");
    key(*list,Qt::Key_Down,Qt::AltModifier); require(order()==original,"Hidden card list movement lost order.");
    key(*list,Qt::Key_H); select(15); list->setFocus();
    for (int i=0;i<12;++i) key(*list,Qt::Key_Up,Qt::AltModifier);
    require(order().indexOf(15)==3 && scope.visibleChannels().contains(15),"Overflow card could not move into the grid.");
    // A large card and its duplicate keep independent display options when moved.
    scope.resetLayout(); rows->setValue(6); columns->setValue(4); select(0);
    key(scope,Qt::Key_Right,Qt::ShiftModifier); key(scope,Qt::Key_Down,Qt::ShiftModifier);
    const auto start=rect(0); move(Qt::Key_Right); const auto right=rect(0);
    require(right.left()>start.left() && right.size()==start.size(),"Moving a spanning card resized it or used the wrong direction.");
    move(Qt::Key_Left); require(rect(0)==start,"Spanning card failed to move back.");
    QTimer::singleShot(0,&scope,[&] {
        if (auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            auto source=d->findChild<QComboBox *>("newCardSource"); source->setCurrentIndex(source->findData(0));
            auto kind=d->findChild<QComboBox *>("newCardView"); kind->setCurrentIndex(kind->findData(1)); d->accept();
        }
    });
    scope.findChild<QPushButton *>("addScopeCard")->click();
    const int duplicate=selected(); scope.setFocus(); const auto duplicateStart=rect(duplicate);
    move(Qt::Key_Right);
    require(rect(duplicate).left()>duplicateStart.left() && selected()==duplicate && rect(0)==start &&
        scope.findChild<QComboBox *>("cardView")->currentData().toInt()==1,"Moving a duplicate changed its original/options/selection.");
    move(Qt::Key_Left);
    scope.toggleSourceVisibility(ScopeTheme::FullMix); select(ScopeTheme::FullMix);
    const auto mixStart=rect(ScopeTheme::FullMix); move(Qt::Key_Up);
    require(rect(ScopeTheme::FullMix).top()<mixStart.top() && selected()==ScopeTheme::FullMix,"Full mix could not move up.");
    // Fullscreen and active gestures retain the same admission rules.
    scope.toggleFullscreen(); scope.setFocus(); select(duplicate); const auto fullStart=rect(duplicate);
    move(Qt::Key_Right); require(rect(duplicate).left()>fullStart.left(),"Fullscreen card move failed.");
    scope.toggleFullscreen(); edit->setChecked(true); scope.setFocus();
    const auto dragStart=scope.panelRect(duplicate).topLeft()+QPointF(36,15);
    mouse(scope,QEvent::MouseButtonPress,dragStart); const auto duringGesture=order(); move(Qt::Key_Left);
    require(order()==duringGesture,"Keyboard move interfered with a mouse gesture.");
    key(scope,Qt::Key_Escape);
    require(player.state().generation==state.generation && player.state().muteMask==state.muteMask &&
        player.state().outputMask==state.outputMask,"Card movement changed audio or transport.");
    scope.resetLayout(); edit->setChecked(false);
    log << "Card movement: four directions, repeats, edges, span/fit/direction guards, hidden/overflow list entries, duplicates, Full mix, fullscreen and input/gesture guards PASS\n";
    log.flush();
}
}
void checkSelectedCardShortcuts(QApplication &app, const QString &directory, QTextStream &log) {
    QTemporaryDir temp(QDir(directory).filePath("card-keys-XXXXXX")); require(temp.isValid(), "Cannot isolate card shortcut checks.");
    struct Restore {
        QApplication &app;
        QVariant settings, banks, themes;
        ~Restore() {
            app.setProperty("settingsFileForTests",settings); app.setProperty("soundFontsDirectoryForTests",banks);
            app.setProperty("themesDirectoryForTests",themes);
        }
    } restore{app,app.property("settingsFileForTests"),app.property("soundFontsDirectoryForTests"),app.property("themesDirectoryForTests")};
    app.setProperty("settingsFileForTests",temp.filePath("settings.ini"));
    app.setProperty("soundFontsDirectoryForTests",temp.path()); app.setProperty("themesDirectoryForTests",temp.filePath("themes"));
    require(QFile::copy(QDir(directory).filePath("fixture-gm.sf2"),temp.filePath("Test.sf2")),"Cannot copy authored bank.");
    PlayerWindow window; auto &scope = window.scopeWindow(); auto &player = window.player();
    player.setVolume(0); window.show(); window.loadFile(QDir(directory).filePath("demo.mid"));
    until(app,[&] { return player.state().valid && !player.state().busy; }); window.refresh();
    player.seek(200,false); until(app,[&] { return !player.state().busy; }); window.refresh();
    log << "Selected-card checks: 16-channel MIDI ready\n"; log.flush();
    checkCardMovement(app, window, log);
    scope.show(); scope.resize(1250,850); scope.activateWindow(); scope.setFocus(); pump(app);
    auto edit = scope.findChild<QPushButton *>("editLayout");
    edit->setChecked(true);
    scope.findChild<QSpinBox *>("gridRows")->setValue(6); scope.findChild<QSpinBox *>("gridColumns")->setValue(4);
    auto list = scope.findChild<QListWidget *>("scopeChannelList");
    auto view = scope.findChild<QComboBox *>("cardView");
    auto levels = scope.findChild<QCheckBox *>("cardLevels");
    const auto item = [&](int id) -> QListWidgetItem * {
        for (int i=0;i<list->count();++i) if (list->item(i)->data(Qt::UserRole).toInt()==id) return list->item(i);
        return nullptr;
    };
    const auto selected = [&] { return list->currentItem()->data(Qt::UserRole).toInt(); };
    const auto hasSpan = [&](int id, int w, int h) {
        return item(id)->toolTip().contains(QString("Size: %1 x %2 cells").arg(w).arg(h));
    };
    const auto focus = [&] {
        scope.activateWindow(); pump(app); scope.setFocus(); pump(app);
        require(QApplication::focusWidget()==&scope,"Cannot focus the visualization canvas.");
    };
    const auto click = [&](int id) {
        focus(); const auto point=scope.panelRect(id).center();
        if (scope.panelRect(id).isEmpty()) std::fprintf(stderr,"Invisible card ID: %d\n",id);
        require(!scope.panelRect(id).isEmpty(),"Selected test card not visible.");
        mouse(scope,QEvent::MouseButtonPress,point); mouse(scope,QEvent::MouseButtonRelease,point);
        require(selected()==id,"Click selected the wrong card.");
    };
    edit->setChecked(false); focus(); key(scope,Qt::Key_Escape); key(scope,Qt::Key_V);
    require(view->currentData().toInt()==0,"A card changed without an explicit selection.");
    key(scope,Qt::Key_Right,Qt::ShiftModifier); require(hasSpan(0,1,1),"Resize selected an implicit card.");
    click(15); const auto generation=player.state().generation;
    QKeyEvent claim(QEvent::ShortcutOverride,Qt::Key_V,Qt::NoModifier);
    claim.setAccepted(false); QApplication::sendEvent(&scope,&claim);
    require(claim.isAccepted() && view->currentData().toInt()==0,"Shortcut override executed a card action or failed to claim it.");
    key(scope,Qt::Key_V,Qt::ShiftModifier); key(scope,Qt::Key_V,Qt::ControlModifier);
    require(view->currentData().toInt()==0,"Modified V was treated as a plain card shortcut.");
    key(scope,Qt::Key_V); require(view->currentData().toInt()==1,"Channel 16 could not cycle its view.");
    key(scope,Qt::Key_V,{},true); require(view->currentData().toInt()==1,"Held view key cycled repeatedly.");
    key(scope,Qt::Key_L); require(levels->isChecked(),"Per-card Levels shortcut failed.");
    key(scope,Qt::Key_M); require(player.state().muteMask==VoiceMask::bit(15),"Mute used a card index instead of source 16.");
    key(scope,Qt::Key_S); require(player.state().soloChannel==15,"Selected-card Solo failed.");
    key(scope,Qt::Key_S); require(player.state().soloChannel==-1 && player.state().muteMask==VoiceMask::bit(15),"Selected Solo did not restore mutes.");
    key(scope,Qt::Key_M); require(player.state().muteMask==0,"Selected mute did not restore voice.");
    key(scope,Qt::Key_Left,Qt::ControlModifier); require(player.state().outputMask==2,"Selection intercepted L/R output shortcut.");
    key(scope,Qt::Key_Left,Qt::ControlModifier);
    key(scope,Qt::Key_H); auto hint=scope.findChild<QLabel *>("cardSelectionHint");
    require(item(15)->checkState()==Qt::Unchecked && hint && hint->isVisible() && hint->text().contains("H to show") && player.state().muteMask==0,
        "Hidden selected card lost restore feedback or muted its source.");
    key(scope,Qt::Key_H,{},true); require(item(15)->checkState()==Qt::Unchecked,"Held hide key toggled visibility.");
    key(scope,Qt::Key_H); require(item(15)->checkState()==Qt::Checked && !hint->isVisible(),"H did not restore the selected card.");
    key(scope,Qt::Key_Up); require(selected()==11,"Grid Up used channel/list order instead of columns.");
    key(scope,Qt::Key_Left); require(selected()==10,"Grid Left did not select its neighbor.");
    edit->setChecked(true); list->setCurrentItem(item(15));
    require(list->model()->moveRow({},list->currentRow(),{},0),"Cannot reorder selected card.");
    pump(app,180); click(15); key(scope,Qt::Key_Right); require(selected()==0,"Navigation ignored reordered grid positions.");
    // Grow the first card to 2x2; navigation must skip its own occupied cells.
    const auto before=scope.panelRect(15), cell=scope.cardRect(0), next=scope.cardRect(5);
    const QPointF start=before.bottomRight()-QPointF(3,3), end=start+next.topLeft()-cell.topLeft();
    mouse(scope,QEvent::MouseButtonPress,start); mouse(scope,QEvent::MouseMove,end); mouse(scope,QEvent::MouseButtonRelease,end);
    pump(app,200); click(15); key(scope,Qt::Key_Right); require(selected()==0,"Right navigation got stuck inside a spanning card.");
    click(15); key(scope,Qt::Key_Down); require(selected()==4,"Down navigation ignored a two-row card.");
    click(15); edit->setChecked(false); focus();
    for (int i=0;i<3;++i) { key(scope,Qt::Key_Left,Qt::ShiftModifier); key(scope,Qt::Key_Up,Qt::ShiftModifier); }
    require(hasSpan(15,1,1),"Keyboard shrink crossed the one-cell minimum.");
    key(scope,Qt::Key_Right,Qt::ShiftModifier);
    key(scope,Qt::Key_Right,Qt::ShiftModifier|Qt::KeypadModifier,true);
    key(scope,Qt::Key_Down,Qt::ShiftModifier);
    require(selected()==15 && hasSpan(15,3,2) && player.state().outputMask==3,"Keyboard grow/repeat changed selection or audio outputs.");
    for (int i=0;i<7;++i) { key(scope,Qt::Key_Right,Qt::ShiftModifier); key(scope,Qt::Key_Down,Qt::ShiftModifier); }
    pump(app,200);
    require(hasSpan(15,4,6) && scope.visibleChannels()==QVector<int>{15},"Keyboard resize ignored grid limits/overflow.");
    // Overflow cards remain adjustable in the list, without changing their source.
    edit->setChecked(true); list->setCurrentItem(item(0)); list->setFocus();
    key(*list,Qt::Key_Right,Qt::ShiftModifier); require(hasSpan(0,2,1),"Overflow card could not be resized from the list.");
    key(*list,Qt::Key_Left,Qt::ShiftModifier);
    list->setCurrentItem(item(15));
    for (int i=0;i<4;++i) key(*list,Qt::Key_Up,Qt::ShiftModifier);
    for (int i=0;i<2;++i) key(*list,Qt::Key_Left,Qt::ShiftModifier);
    require(hasSpan(15,2,2) && scope.visibleChannels().contains(0),"Shrinking did not restore overflowing neighbors.");
    // Growth is rejected if it would lose the selected visible card.
    list->setCurrentItem(item(0));
    for (int i=0;i<5;++i) key(*list,Qt::Key_Down,Qt::ShiftModifier);
    key(*list,Qt::Key_Right,Qt::ShiftModifier); key(*list,Qt::Key_Right,Qt::ShiftModifier);
    require(hasSpan(0,2,6) && scope.visibleChannels().contains(0),"Resize pushed the selected card outside the grid.");
    for (int i=0;i<5;++i) key(*list,Qt::Key_Up,Qt::ShiftModifier);
    key(*list,Qt::Key_Left,Qt::ShiftModifier); pump(app,200);
    QTimer::singleShot(0,&scope,[&] {
        if (auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            auto source=d->findChild<QComboBox *>("newCardSource"); source->setCurrentIndex(source->findData(15));
            auto kind=d->findChild<QComboBox *>("newCardView"); kind->setCurrentIndex(kind->findData(0)); d->accept();
        }
    });
    list->setCurrentItem(item(15)); scope.findChild<QPushButton *>("addScopeCard")->click();
    const int duplicate=selected(); click(duplicate); key(scope,Qt::Key_V);
    require(view->currentData().toInt()==1,"Duplicate card did not cycle independently.");
    key(scope,Qt::Key_H); key(scope,Qt::Key_Right,Qt::ShiftModifier);
    require(hasSpan(duplicate,2,1) && hasSpan(15,2,2),"Hidden duplicate resize changed its original.");
    key(scope,Qt::Key_Left,Qt::ShiftModifier); key(scope,Qt::Key_H); pump(app,200);
    key(scope,Qt::Key_V); require(view->currentData().toInt()==2,"Duplicate card keyboard view missing.");
    click(15); require(view->currentData().toInt()==1 && levels->isChecked(),"Duplicate shortcuts changed the original card.");
    click(duplicate); key(scope,Qt::Key_M); require(player.state().muteMask==VoiceMask::bit(15),"Duplicate mute targeted its card ID."); key(scope,Qt::Key_M);
    key(scope,Qt::Key_V); require(view->currentData().toInt()==3,"MIDI Piano roll was skipped.");
    key(scope,Qt::Key_V); require(view->currentData().toInt()==0,"Unsupported Pattern was included for MIDI.");
    scope.toggleSourceVisibility(ScopeTheme::FullMix); click(ScopeTheme::FullMix);
    key(scope,Qt::Key_M); key(scope,Qt::Key_S); require(player.state().muteMask==0 && player.state().soloChannel==-1,"Full mix changed voice audio.");
    key(scope,Qt::Key_V); key(scope,Qt::Key_L); require(view->currentData().toInt()==1 && levels->isChecked(),"Full mix card commands failed.");
    key(scope,Qt::Key_Right,Qt::ShiftModifier); require(hasSpan(ScopeTheme::FullMix,2,1),"Full mix keyboard resize failed.");
    key(scope,Qt::Key_Left,Qt::ShiftModifier);
    bool dialogSeen=false;
    QTimer::singleShot(0,&scope,[&] {
        auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d) return;
        dialogSeen=d->objectName()=="scopeColorsDialog" && d->windowTitle()=="Card settings";
        key(scope,Qt::Key_H); key(scope,Qt::Key_Right,Qt::ShiftModifier); d->reject();
    });
    key(scope,Qt::Key_Return); require(dialogSeen && item(ScopeTheme::FullMix)->checkState()==Qt::Checked,"Enter settings or modal protection failed.");
    require(hasSpan(ScopeTheme::FullMix,1,1),"Keyboard resize escaped a modal dialog.");
    click(duplicate);
    auto field=scope.findChild<QSpinBox *>("gridRows")->findChild<QLineEdit *>(); field->setFocus();
    key(scope,Qt::Key_V); key(scope,Qt::Key_M); require(view->currentData().toInt()==0 && player.state().muteMask==0,"Card command escaped a field through its parent.");
    key(scope,Qt::Key_Right,Qt::ShiftModifier); require(hasSpan(duplicate,1,1),"Keyboard resize escaped a focused field.");
    view->setFocus(); key(*view,Qt::Key_M); require(player.state().muteMask==0,"Card mute escaped a dropdown.");
    view->showPopup(); pump(app); key(scope,Qt::Key_H); require(item(duplicate)->checkState()==Qt::Checked,"Card hide escaped a popup.");
    view->hidePopup(); pump(app,250);
    // Synthetic popup activation can leave Qt's private container visible after
    // hidePopup. Dismiss it before asserting canvas focus; input must stay blocked until then.
    if (auto popup=QApplication::activePopupWidget()) popup->hide();
    until(app,[] { return !QApplication::activePopupWidget(); });
    focus(); key(window,Qt::Key_V); require(view->currentData().toInt()==0,"Selected-card command escaped into the main player.");
    list->setFocus(); list->setCurrentItem(item(15)); key(*list,Qt::Key_Down);
    require(selected()==duplicate,"Cards list lost native arrow navigation.");
    key(*list,Qt::Key_V); require(view->currentData().toInt()==1,"Cards list did not support selected-card commands.");
    key(*list,Qt::Key_Escape);
    const auto sameRow=list->visualItemRect(list->currentItem()).center();
    mouse(*list->viewport(),QEvent::MouseButtonPress,sameRow); mouse(*list->viewport(),QEvent::MouseButtonRelease,sameRow);
    key(*list,Qt::Key_V); require(view->currentData().toInt()==2,"Clicking the same list row after Escape did not restore selection.");
    view->setCurrentIndex(view->findData(1));
    focus(); key(scope,Qt::Key_Escape); key(scope,Qt::Key_V); require(view->currentData().toInt()==1,"Escape did not clear shortcut selection.");
    key(scope,Qt::Key_Right);
    require(selected()==15,"Arrow key did not start selection at the first visible card.");
    edit->setChecked(false); click(15); pump(app);
    scope.grab().save(QDir(directory).filePath("native-selected-card.png"));
    scope.toggleFullscreen(); focus(); key(scope,Qt::Key_V); require(view->currentData().toInt()==2,"Fullscreen selected-card view failed.");
    key(scope,Qt::Key_Right,Qt::ShiftModifier); require(hasSpan(15,3,2),"Fullscreen keyboard resize failed.");
    key(scope,Qt::Key_Left,Qt::ShiftModifier);
    key(scope,Qt::Key_Escape); require(!scope.presentationFullscreen(),"Escape selection intercepted fullscreen exit.");
    key(scope,Qt::Key_Escape); key(scope,Qt::Key_V); require(view->currentData().toInt()==2,"Second Escape failed to clear selection.");
    require(player.state().generation==generation && player.state().outputMask==3 && !player.state().busy,"Card commands restarted playback or changed transport.");
    edit->setChecked(true); list->setCurrentItem(item(duplicate)); scope.findChild<QPushButton *>("removeScopeCard")->click();
    require(!item(duplicate),"Selected card removal failed.");
    focus(); key(scope,Qt::Key_V); require(!item(duplicate),"Shortcut recreated a removed card.");
    scope.resetLayout(); focus(); key(scope,Qt::Key_V); require(view->currentData().toInt()==0,"Reset retained active selection.");
    click(15); window.loadFile(QDir(directory).filePath("demo.nsf"));
    until(app,[&] {return !player.state().busy;}); window.refresh(); focus(); key(scope,Qt::Key_V);
    require(view->currentData().toInt()==0 && !hint->isVisible(),"New file reused old selection.");
    require(player.state().outputErrors==0 && player.state().starvations==0,"Card shortcuts interrupted audio output.");
    window.close();
    log << "Selected-card shortcuts: channel 16, spatial/spanning/reordered navigation, keyboard resize/limits/overflow, duplicates, hidden restore, Levels, mute/Solo, Full mix, fields/popups/dialogs, fullscreen and reset/file lifecycle PASS\n";
    log.flush();
}

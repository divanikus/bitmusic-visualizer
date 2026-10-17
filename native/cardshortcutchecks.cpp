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
    click(15); const auto generation=player.state().generation;
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
    QTimer::singleShot(0,&scope,[&] {
        if (auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            auto source=d->findChild<QComboBox *>("newCardSource"); source->setCurrentIndex(source->findData(15));
            auto kind=d->findChild<QComboBox *>("newCardView"); kind->setCurrentIndex(kind->findData(0)); d->accept();
        }
    });
    list->setCurrentItem(item(15)); scope.findChild<QPushButton *>("addScopeCard")->click();
    const int duplicate=selected(); click(duplicate); key(scope,Qt::Key_V);
    require(view->currentData().toInt()==1,"Duplicate card did not cycle independently.");
    key(scope,Qt::Key_V); require(view->currentData().toInt()==2,"Duplicate card keyboard view missing.");
    click(15); require(view->currentData().toInt()==1 && levels->isChecked(),"Duplicate shortcuts changed the original card.");
    click(duplicate); key(scope,Qt::Key_M); require(player.state().muteMask==VoiceMask::bit(15),"Duplicate mute targeted its card ID."); key(scope,Qt::Key_M);
    key(scope,Qt::Key_V); require(view->currentData().toInt()==3,"MIDI Piano roll was skipped.");
    key(scope,Qt::Key_V); require(view->currentData().toInt()==0,"Unsupported Pattern was included for MIDI.");
    scope.toggleSourceVisibility(ScopeTheme::FullMix); click(ScopeTheme::FullMix);
    key(scope,Qt::Key_M); key(scope,Qt::Key_S); require(player.state().muteMask==0 && player.state().soloChannel==-1,"Full mix changed voice audio.");
    key(scope,Qt::Key_V); key(scope,Qt::Key_L); require(view->currentData().toInt()==1 && levels->isChecked(),"Full mix card commands failed.");
    bool dialogSeen=false;
    QTimer::singleShot(0,&scope,[&] {
        auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d) return;
        dialogSeen=d->objectName()=="scopeColorsDialog" && d->windowTitle()=="Card settings";
        key(scope,Qt::Key_H); d->reject();
    });
    key(scope,Qt::Key_Return); require(dialogSeen && item(ScopeTheme::FullMix)->checkState()==Qt::Checked,"Enter settings or modal protection failed.");
    click(duplicate);
    auto field=scope.findChild<QSpinBox *>("gridRows")->findChild<QLineEdit *>(); field->setFocus();
    key(scope,Qt::Key_V); key(scope,Qt::Key_M); require(view->currentData().toInt()==0 && player.state().muteMask==0,"Card command escaped a field through its parent.");
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
    log << "Selected-card shortcuts: channel 16, spatial/spanning/reordered navigation, duplicates, hidden restore, Levels, mute/Solo, Full mix, fields/popups/dialogs, fullscreen and reset/file lifecycle PASS\n";
    log.flush();
}

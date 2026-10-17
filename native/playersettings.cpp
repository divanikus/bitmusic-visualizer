#include "playerwindow.h"
#include "playersettings.h"
#include "appsettings.h"
#include "renderersettings.h"
#include "romfiles.h"
#include "miditrack.h"
#include "mt32track.h"
#include "palette.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTimeEdit>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

int unknownTrackLimitPreference() {
    QSettings settings(playerSettingsPath(),playerSettingsFormat());settings.setFallbacksEnabled(false);
    bool ok=false;const int ms=settings.value("Playback/UnknownDurationLimitMs",0).toInt(&ok);
    return ok && ms>=1000 && ms<=86399000?ms:0;
}
namespace {
QLabel *hint(const QString &text) {
    auto label=new QLabel(text);label->setWordWrap(true);label->setObjectName("settingsHint");return label;
}
bool saveSettings(QWidget *parent,const QVariantMap &values) {
    QSettings settings(playerSettingsPath(),playerSettingsFormat());settings.setFallbacksEnabled(false);
    for(auto it=values.cbegin();it!=values.cend();++it)settings.setValue(it.key(),it.value());
    settings.sync();
    if(settings.status()==QSettings::NoError)return true;
    QMessageBox::warning(parent,"Could not save settings","The settings could not be saved to:\n"+playerSettingsPath()+"\n\nPlease check that this location is writable.");return false;
}
struct ResourceStatus { QString file,path,status,detail; bool found=false; };
ResourceStatus inspectResource(const QString &name,const QString &path,qint64 size=0) {
    ResourceStatus row{name,path,"Missing","Place this file in the folder shown below."};
    QFile file(path);
    if(!file.exists())return row;
    if(!file.open(QIODevice::ReadOnly)) {row.status="Unreadable";row.detail=file.errorString();return row;}
    if(file.size()<=0 || (size && file.size()!=size) || (!size && file.size()>2*1024*1024)) {
        row.status="Invalid size";row.detail=size?QString("Expected %1 bytes.").arg(size):"Expected a complete Control or PCM ROM image.";return row;
    }
    row.status=size?"Found (size OK)":"Found";row.found=true;
    row.detail=size?"File size matches. Contents are not verified by this check.":"Munt checks ROM identity and matching Control/PCM models when opening music.";
    return row;
}
}
void PlayerWindow::showSettings() {
    if(auto existing=findChild<QDialog *>("playerSettings")) {existing->show();existing->raise();existing->activateWindow();return;}
    auto dialog=new QDialog(this);dialog->setObjectName("playerSettings");dialog->setWindowTitle("Settings");
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setPalette(playerPalette());dialog->resize(540,530);
    dialog->setStyleSheet(R"(
        QLabel#settingsHint { color: #92a6b0; }
        QComboBox, QTimeEdit, QLineEdit { background: #202e35; color: #dce5e8; border: 1px solid #465b62; border-radius: 4px; padding: 5px; min-height: 22px; }
        QTreeWidget { background: #1b252b; color: #dce5e8; border: 1px solid #314047; }
        QTreeWidget::item { height: 28px; }
        QTreeWidget::item:selected { background: #294c48; color: #dce5e8; }
        QHeaderView::section { background: #253239; color: #a2e5d5; border: none; padding: 6px; }
    )");
    auto root=new QVBoxLayout(dialog);auto tabs=new QTabWidget;tabs->setObjectName("settingsTabs");root->addWidget(tabs,1);
    auto page=[&](const QString &title) {auto widget=new QWidget;auto layout=new QVBoxLayout(widget);layout->setContentsMargins(14,16,14,14);layout->setSpacing(12);tabs->addTab(widget,title);return layout;};
    auto playback=page("Playback");
    auto limit=new QCheckBox("Limit tracks without a known duration");limit->setObjectName("unknownDurationEnabled");
    const int saved=unknownTrackLimitPreference();limit->setChecked(saved>0);playback->addWidget(limit);
    auto duration=new QTimeEdit;duration->setObjectName("unknownDurationTime");duration->setDisplayFormat("HH:mm:ss");
    duration->setMinimumTime(QTime(0,0,1));duration->setMaximumTime(QTime(23,59,59));
    QSettings settings(playerSettingsPath(),playerSettingsFormat());settings.setFallbacksEnabled(false);
    const int last=std::clamp(settings.value("Playback/LastUnknownDurationLimitMs",180000).toInt(),1000,86399000);
    duration->setTime(QTime(0,0).addMSecs(saved?saved:last));duration->setEnabled(limit->isChecked());
    auto form=new QFormLayout;form->addRow("Maximum duration",duration);playback->addLayout(form);
    playback->addWidget(hint("Without a limit, tracks with no known end play indefinitely. A confirmed end or loop still advances the playlist according to Repeat."));
    playback->addWidget(hint("Applies to the next track. The current track keeps its original limit."));playback->addStretch();
    auto applyLimit=[this,dialog,limit,duration] {
        duration->setEnabled(limit->isChecked());
        const int ms=QTime(0,0).msecsTo(duration->time());
        if(saveSettings(dialog,{{"Playback/LastUnknownDurationLimitMs",ms},{"Playback/UnknownDurationLimitMs",limit->isChecked()?ms:0}}))player_.setUnknownDurationLimit(limit->isChecked()?ms:0);
    };
    connect(limit,&QCheckBox::toggled,dialog,applyLimit);
    connect(duration,&QTimeEdit::editingFinished,dialog,applyLimit);

    auto rendering=page("Rendering");auto renderForm=new QFormLayout;
    auto renderer=new QComboBox;renderer->setObjectName("rendererChoice");styleCombo(renderer);
    renderer->addItem("Auto (recommended)","auto");
#ifdef Q_OS_WIN
    renderer->addItem("Direct3D 11","d3d11");
#endif
    renderer->addItem("OpenGL","opengl");renderer->addItem("CPU (no effects)","cpu");
    renderer->setCurrentIndex(renderer->findData(scopeRendererPreference()));renderForm->addRow("Renderer",renderer);
    auto fps=new QComboBox;fps->setObjectName("waveFrameRate");styleCombo(fps);
    for(int rate:{30,60,120})fps->addItem(QString("%1 FPS").arg(rate),rate);
    fps->setCurrentIndex(fps->findData(scopes_.frameRate()));renderForm->addRow("Frame rate",fps);rendering->addLayout(renderForm);
    auto current=hint("Currently using: "+scopes_.rendererName());current->setObjectName("currentRenderer");rendering->addWidget(current);
    rendering->addWidget(hint("Renderer changes take effect after restarting the app. CPU rendering has no trail or glow."));
    rendering->addWidget(hint("Frame rate applies immediately. Higher rates use more CPU/GPU; actual performance depends on your hardware."));rendering->addStretch();
    connect(renderer,&QComboBox::currentIndexChanged,dialog,[dialog,renderer] {saveSettings(dialog,{{"Renderer/Backend",renderer->currentData()}});});
    connect(fps,&QComboBox::currentIndexChanged,dialog,[this,fps] {scopes_.setFrameRate(fps->currentData().toInt());scopes_.saveFrameRate();});

    auto resources=page("Resources");
    resources->addWidget(hint("Only some formats need these files. Missing resources do not affect other music."));
    auto files=new QTreeWidget;files->setObjectName("resourceFiles");files->setHeaderLabels({"Resource file","Status"});files->setRootIsDecorated(false);
    files->setSelectionMode(QAbstractItemView::SingleSelection);files->header()->setSectionResizeMode(0,QHeaderView::Stretch);files->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    resources->addWidget(files,1);auto detail=hint({});detail->setObjectName("resourceDetail");resources->addWidget(detail);
    auto path=new QLineEdit;path->setReadOnly(true);path->setObjectName("resourcePath");resources->addWidget(path);
    auto actions=new QHBoxLayout;auto open=new QPushButton("Open folder");open->setObjectName("resourceFolder");auto refresh=new QPushButton("Refresh");refresh->setObjectName("refreshResources");
    actions->addWidget(open);actions->addStretch();actions->addWidget(refresh);resources->addLayout(actions);
    auto refreshFiles=[this,files] {
        const int selected=files->currentIndex().row();files->clear();const QDir roms(playerRomsDirectory());
        QVector<ResourceStatus> rows;
        for(const auto &name:{"MT32_CONTROL.ROM","MT32_PCM.ROM"})rows<<inspectResource(name,QDir(playerMt32Directory()).filePath(name));
        rows<<inspectResource("kernal.rom",roms.filePath("c64/kernal.rom"),8192)
            <<inspectResource("basic.rom",roms.filePath("c64/basic.rom"),8192)
            <<inspectResource("chargen.rom",roms.filePath("c64/chargen.rom"),4096)
            <<inspectResource("ym2608_adpcm_rom.bin",roms.filePath("ymfm/ym2608_adpcm_rom.bin"),8192);
        const auto banks=availableSoundFonts();
        rows<<ResourceStatus{"SoundFonts (*.sf2)",playerSoundFontsDirectory(),banks.isEmpty()?"Missing":QString("%1 found").arg(banks.size()),banks.isEmpty()?"General MIDI needs a user-supplied SF2 bank.":"Choose a bank in the player when MIDI is open. Banks are validated during playback setup.",!banks.isEmpty()};
        for(const auto &row:rows) {
            auto item=new QTreeWidgetItem(files,{row.file,row.status});item->setData(0,Qt::UserRole,row.path);item->setData(0,Qt::UserRole+1,row.detail);
            item->setData(0,Qt::UserRole+2,row.file.startsWith("SoundFonts"));item->setToolTip(0,row.path);item->setToolTip(1,row.detail);
            item->setForeground(1,QColor(row.found?"#a2e5d5":"#c2abb0"));
        }
        files->setCurrentItem(files->topLevelItem(std::clamp(selected,0,int(rows.size())-1)));refreshSoundFonts();
    };
    connect(files,&QTreeWidget::currentItemChanged,dialog,[path,detail](QTreeWidgetItem *item) {
        path->setText(item?QDir::toNativeSeparators(item->data(0,Qt::UserRole).toString()):QString());path->setCursorPosition(0);
        detail->setText(item?item->data(0,Qt::UserRole+1).toString():QString());
    });
    connect(refresh,&QPushButton::clicked,dialog,refreshFiles);
    connect(open,&QPushButton::clicked,dialog,[dialog,files] {
        auto item=files->currentItem();if(!item)return;
        const auto path=item->data(0,Qt::UserRole).toString();const auto folder=item->data(0,Qt::UserRole+2).toBool()?path:QFileInfo(path).absolutePath();
        if(!QDir().mkpath(folder) || !QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))QMessageBox::warning(dialog,"Could not open folder",QDir::toNativeSeparators(folder));
    });
    refreshFiles();
    auto footer=new QHBoxLayout;auto savedHint=hint("Settings are saved automatically.");savedHint->setWordWrap(false);footer->addWidget(savedHint);footer->addStretch();
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Close);footer->addWidget(buttons);root->addLayout(footer);
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
    dialog->show();
}

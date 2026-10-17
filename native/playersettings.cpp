#include "playersettings.h"
#include "appsettings.h"
#include "renderersettings.h"
#include "resourcestatus.h"
#include "palette.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
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
#include <algorithm>

int unknownTrackLimitPreference() {
    QSettings settings(playerSettingsPath(),playerSettingsFormat());settings.setFallbacksEnabled(false);
    bool ok=false;const int ms=settings.value("Playback/UnknownDurationLimitMs",0).toInt(&ok);
    return ok && ms>=1000 && ms<=86399000?ms:0;
}
namespace {
enum ResourceRole { PathRole = Qt::UserRole, DetailRole, DirectoryRole };
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
}
PlayerSettingsDialog::PlayerSettingsDialog(int frameRate, const QString &rendererName, QWidget *parent) : QDialog(parent) {
    setObjectName("playerSettings");setWindowTitle("Settings");
    setAttribute(Qt::WA_DeleteOnClose);setPalette(playerPalette());resize(540,530);
    setStyleSheet(R"(
        QLabel#settingsHint { color: #92a6b0; }
        QComboBox, QTimeEdit, QLineEdit { background: #202e35; color: #dce5e8; border: 1px solid #465b62; border-radius: 4px; padding: 5px; min-height: 22px; }
        QTreeWidget { background: #1b252b; color: #dce5e8; border: 1px solid #314047; }
        QTreeWidget::item { height: 28px; }
        QTreeWidget::item:selected { background: #294c48; color: #dce5e8; }
        QHeaderView::section { background: #253239; color: #a2e5d5; border: none; padding: 6px; }
    )");
    auto root=new QVBoxLayout(this);auto tabs=new QTabWidget;tabs->setObjectName("settingsTabs");root->addWidget(tabs,1);
    auto page=[&](const QString &title) {auto widget=new QWidget;auto layout=new QVBoxLayout(widget);layout->setContentsMargins(14,16,14,14);layout->setSpacing(12);tabs->addTab(widget,title);return layout;};
    setupPlayback(page("Playback"));
    setupRendering(page("Rendering"), frameRate, rendererName);
    setupResources(page("Resources"));
    auto footer=new QHBoxLayout;auto savedHint=hint("Settings are saved automatically.");savedHint->setWordWrap(false);footer->addWidget(savedHint);footer->addStretch();
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Close);footer->addWidget(buttons);root->addLayout(footer);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::close);
}
void PlayerSettingsDialog::setupPlayback(QVBoxLayout *playback) {
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
    auto applyLimit=[this,limit,duration] {
        duration->setEnabled(limit->isChecked());
        const int ms=QTime(0,0).msecsTo(duration->time());
        if(saveSettings(this,{{"Playback/LastUnknownDurationLimitMs",ms},{"Playback/UnknownDurationLimitMs",limit->isChecked()?ms:0}})) {
            if (onUnknownDurationLimitChanged) onUnknownDurationLimitChanged(limit->isChecked()?ms:0);
        }
    };
    connect(limit,&QCheckBox::toggled,this,applyLimit);
    connect(duration,&QTimeEdit::editingFinished,this,applyLimit);
}
void PlayerSettingsDialog::setupRendering(QVBoxLayout *rendering, int frameRate, const QString &rendererName) {
    auto renderForm=new QFormLayout;
    auto renderer=new QComboBox;renderer->setObjectName("rendererChoice");styleCombo(renderer);
    renderer->addItem("Auto (recommended)","auto");
#ifdef Q_OS_WIN
    renderer->addItem("Direct3D 11","d3d11");
#endif
    renderer->addItem("OpenGL","opengl");renderer->addItem("CPU (no effects)","cpu");
    renderer->setCurrentIndex(renderer->findData(scopeRendererPreference()));renderForm->addRow("Renderer",renderer);
    auto fps=new QComboBox;fps->setObjectName("waveFrameRate");styleCombo(fps);
    for(int rate:{30,60,120})fps->addItem(QString("%1 FPS").arg(rate),rate);
    fps->setCurrentIndex(fps->findData(frameRate));renderForm->addRow("Frame rate",fps);rendering->addLayout(renderForm);
    auto current=hint("Currently using: "+rendererName);current->setObjectName("currentRenderer");rendering->addWidget(current);
    rendering->addWidget(hint("Renderer changes take effect after restarting the app. CPU rendering has no trail or glow."));
    rendering->addWidget(hint("Frame rate applies immediately. Higher rates use more CPU/GPU; actual performance depends on your hardware."));rendering->addStretch();
    connect(renderer,&QComboBox::currentIndexChanged,this,[this,renderer] {saveSettings(this,{{"Renderer/Backend",renderer->currentData()}});});
    connect(fps,&QComboBox::currentIndexChanged,this,[this,fps] {if (onFrameRateChanged) onFrameRateChanged(fps->currentData().toInt());});
}
void PlayerSettingsDialog::setupResources(QVBoxLayout *resources) {
    resources->addWidget(hint("Only some formats need these files. Missing resources do not affect other music."));
    auto files=new QTreeWidget;resourceFiles_=files;files->setObjectName("resourceFiles");files->setHeaderLabels({"Resource file","Status"});files->setRootIsDecorated(false);
    files->setSelectionMode(QAbstractItemView::SingleSelection);files->header()->setSectionResizeMode(0,QHeaderView::Stretch);files->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    resources->addWidget(files,1);auto detail=hint({});detail->setObjectName("resourceDetail");resources->addWidget(detail);
    auto path=new QLineEdit;path->setReadOnly(true);path->setObjectName("resourcePath");resources->addWidget(path);
    auto actions=new QHBoxLayout;auto open=new QPushButton("Open folder");open->setObjectName("resourceFolder");auto refresh=new QPushButton("Refresh");refresh->setObjectName("refreshResources");
    actions->addWidget(open);actions->addStretch();actions->addWidget(refresh);resources->addLayout(actions);
    connect(files,&QTreeWidget::currentItemChanged,this,[path,detail](QTreeWidgetItem *item) {
        path->setText(item?QDir::toNativeSeparators(item->data(0,PathRole).toString()):QString());path->setCursorPosition(0);
        detail->setText(item?item->data(0,DetailRole).toString():QString());
    });
    connect(refresh,&QPushButton::clicked,this,&PlayerSettingsDialog::refreshResources);
    connect(open,&QPushButton::clicked,this,&PlayerSettingsDialog::openResourceFolder);
}
void PlayerSettingsDialog::refreshResources() {
    const int selected=resourceFiles_->currentIndex().row();resourceFiles_->clear();
    const auto rows = inspectPlayerResources();
    for(const auto &row:rows) {
        auto item=new QTreeWidgetItem(resourceFiles_,{row.file,row.status});item->setData(0,PathRole,row.path);item->setData(0,DetailRole,row.detail);
        item->setData(0,DirectoryRole,row.directory);item->setToolTip(0,row.path);item->setToolTip(1,row.detail);
        item->setForeground(1,QColor(row.found?"#a2e5d5":"#c2abb0"));
    }
    resourceFiles_->setCurrentItem(resourceFiles_->topLevelItem(std::clamp(selected,0,int(rows.size())-1)));
    if (onResourcesRefreshed) onResourcesRefreshed();
}
void PlayerSettingsDialog::openResourceFolder() {
    auto item=resourceFiles_->currentItem();if(!item)return;
    const auto path=item->data(0,PathRole).toString();const auto folder=item->data(0,DirectoryRole).toBool()?path:QFileInfo(path).absolutePath();
    if(!QDir().mkpath(folder) || !QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))QMessageBox::warning(this,"Could not open folder",QDir::toNativeSeparators(folder));
}

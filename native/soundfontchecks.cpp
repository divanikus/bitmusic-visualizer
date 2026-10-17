#include "soundfontbank.h"
#include "soundfontlist.h"
#include "midicontrols.h"
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <future>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
void write(const QString &path,const QByteArray &data) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data)==data.size(),"Cannot write SoundFont cache fixture.");
}
bool rejects(const QString &name) {
    try { loadSoundFontBanks(name); } catch(const std::exception &) { return true; }
    return false;
}
}

void checkSoundFontLists(const QString &directory,const QByteArray &fixture,QTextStream &log) {
    QTemporaryDir temp(QDir(directory).filePath("external-set-XXXXXX"));
    require(temp.isValid() && QDir().mkpath(temp.filePath("nested")),"Cannot create list fixtures.");
    const auto first=temp.filePath("First bank.sf2"), second=temp.filePath("Second.sf2");
    const auto root=temp.filePath("Main.sflist"), nested=temp.filePath("nested/part.sflist.json");
    write(first,fixture); write(second,fixture);
    write(root,"# Included set\r\n\"nested\\part.sflist.json\"\r\nSecond.sf2\r\n");
    write(nested,QByteArray::fromHex("efbbbf")+R"({"soundFonts":[{"fileName":"../First bank.sf2"}]})");
    const QStringList expected{QFileInfo(first).canonicalFilePath(),QFileInfo(second).canonicalFilePath()};
    require(soundFontPaths(root)==expected,"Nested text/JSON paths or ordering failed.");
    const auto banks=loadSoundFontBanks(root);
    require(banks.size()==2 && banks[0].name=="First bank.sf2" && banks[1].bytes==fixture,"External set failed to load.");
    write(root,"nested/part.sflist.json\nSecond.sf2\nFirst bank.sf2\n");
    require(soundFontPaths(root)==QStringList{expected[1],expected[0]},"Repeated bank did not keep its final precedence.");
    selectSoundFont(root);
    require(selectedSoundFont()==QFileInfo(root).canonicalFilePath() && availableSoundFonts().contains(selectedSoundFont()),
            "External selection was not saved/discoverable.");
    require(!selectSoundFont(root),"Reselecting an external set changed the selection.");
    require(!QFileInfo::exists(QDir(directory).filePath("Main.sflist")),"External list was copied into the resource folder.");

    const auto invalid=temp.filePath("Invalid.sflist");
    const auto rejectsText=[&](const QByteArray &text,const char *error) {
        write(invalid,text);
        try { selectSoundFont(invalid); } catch(const std::exception &e) {
            return QByteArray(e.what()).contains(error) && selectedSoundFont()==QFileInfo(root).canonicalFilePath();
        }
        return false;
    };
    require(rejectsText("{", "Invalid SoundFont JSON"),"Malformed JSON changed selection or fell back to text.");
    require(rejectsText(R"({"soundFonts":["First bank.sf2"]})", "fileName"),"Invalid JSON entry accepted.");
    require(rejectsText(R"({"soundFonts":[]})", "no banks"),"Empty JSON set accepted.");
    require(rejectsText(R"({"soundFonts":[{"fileName":""}]})", "empty fileName"),"Empty path became a directory.");
    for(const auto *field:{"gain","channels","patchMappings"})
        require(rejectsText(QByteArray("{\"soundFonts\":[{\"fileName\":\"First bank.sf2\",\"")+field+"\":null}]}","BASSMIDI"),
                "BASSMIDI rule was silently discarded.");
    require(rejectsText(R"({"soundFonts":[{"fileName":"First bank.sf2","unknown":0}]})", "Unsupported"),"Unknown field accepted.");
    require(rejectsText("Other.sf3", "not supported"),"Unsupported bank format was not identified.");
    require(rejectsText("Missing.sf2", "missing"),"Missing bank accepted.");
    require(rejectsText("Invalid.sflist", "cycle"),"List reference cycle accepted.");
    write(nested,"../Invalid.sflist"); // JSON extension must remain JSON, even when text names a file.
    require(rejectsText("nested/part.sflist.json", "Invalid SoundFont JSON"),"JSON extension silently read as text.");
    write(temp.filePath("cycle.sflist"),"Invalid.sflist");
    require(rejectsText("cycle.sflist", "cycle"),"Indirect list cycle accepted.");
    for(int i=0;i<8;++i) write(temp.filePath(QString("Depth%1.sflist").arg(i)),i==7 ? QByteArray("First bank.sf2") :
        QString("Depth%1.sflist").arg(i+1).toUtf8());
    require(soundFontPaths(temp.filePath("Depth0.sflist")).size()==1 &&
            rejectsText("Depth0.sflist", "nesting"),"List nesting limit incorrect.");
    write(temp.filePath("Large.sflist"),QByteArray(32768,' ')+"First bank.sf2");
    require(rejectsText("Large.sflist\nLarge.sflist", "64 KiB"),"Combined list size limit missing.");
    QByteArray many;
    for(int i=0;i<33;++i) {
        const auto name=QString("Bank%1.sf2").arg(i); write(temp.filePath(name),fixture); many+=name.toUtf8()+'\n';
    }
    require(rejectsText(many, "32 banks"),"Unique bank limit missing.");
    require(QFile::remove(root),"Cannot remove selected list fixture.");
    require(selectedSoundFont()==root && !availableSoundFonts().contains(root) && rejects(root),
            "Missing external selection silently fell back to a local bank.");
    selectSoundFont("Test.sf2");
    require(!selectSoundFont(QDir(directory).filePath("Test.sf2")) && selectedSoundFont()=="Test.sf2",
            "Browsing an existing local bank caused a reload or lost portable selection.");
    clearSoundFontBankCache();
    log << "SoundFont lists: nested JSON/text, relative paths, precedence, external selection, limits and explicit unsupported-rule errors PASS\n";
}

void checkSoundFontBrowse(const QString &directory,const QString &midi,QTextStream &log) {
    QTemporaryDir temp(QDir(directory).filePath("browse-set-XXXXXX")); require(temp.isValid(),"Cannot create browse fixture.");
    const auto path=temp.filePath("External.json");
    write(path,R"({"soundFonts":[{"fileName":"../Test.sf2"}]})");
    struct DialogMode {
        bool previous=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        DialogMode() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }
        ~DialogMode() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,previous); }
    } dialogMode;
    MidiControls controls; controls.prepareSource(midi);
    auto combo=controls.findChild<QComboBox *>("soundFont");
    int reloads=0;
    controls.onReloadRequested=[&](const QString &source) { require(source==midi,"Browse reloaded another MIDI."); ++reloads; };
    const auto browse=[&](const QString &file) {
        bool found=false, expired=false;
        QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,&controls,[&] {
            expired=true;
            if(auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        timer.start(3000);
        QTimer::singleShot(0,&controls,[&] {
            if(auto dialog=qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                found=true;
                if(file.isEmpty()) dialog->reject();
                else { dialog->selectFile(file); QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection); }
            }
        });
        const int at=combo->findText("Browse..."); require(at>=0,"SoundFont Browse action missing.");
        combo->setCurrentIndex(at);
        QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,at));
        require(found && !expired && combo->currentData().toString()==selectedSoundFont(),"Browse dialog or selection restoration failed.");
    };
    browse({}); require(reloads==0 && selectedSoundFont()=="Test.sf2","Browse cancellation changed playback.");
    browse(QDir(directory).filePath("Test.sf2")); require(reloads==0,"Browsing the current local bank restarted playback.");
    browse(path); require(reloads==1 && selectedSoundFont()==QFileInfo(path).canonicalFilePath(),"External browse did not persist/reload once.");
    browse(path); require(reloads==1,"Reselecting the external set restarted playback.");
    MidiControls reopened; reopened.prepareSource(midi);
    require(reopened.findChild<QComboBox *>("soundFont")->currentData().toString()==selectedSoundFont(),"Recreated controls lost external selection.");
    selectSoundFont("Test.sf2");
    log << "SoundFont Browse: external JSON, saved selection, cancel and same-file reload guards PASS\n";
}

void checkSoundFontBankCache(const QString &directory,const QByteArray &fixture,QTextStream &log) {
    const QDir folder(directory);
    const auto firstPath=folder.filePath("CacheFirst.sf2"), secondPath=folder.filePath("CacheSecond.sf2");
    const auto listPath=folder.filePath("Cache.sflist");
    write(firstPath,fixture); write(secondPath,fixture);
    write(listPath,"CacheFirst.sf2\nCacheSecond.sf2\n");
    clearSoundFontBankCache();
    const auto original=loadSoundFontBanks("Cache.sflist");
    auto repeated=loadSoundFontBanks("Cache.sflist");
    require(original.size()==2 && repeated.size()==2,"Cached SFLIST lost a bank.");
    for(int i=0;i<2;++i)
        require(original[i].bytes.constData()==repeated[i].bytes.constData(),"Unchanged bank was reread instead of sharing input bytes.");

    // Worker readers share immutable storage; each will later have its own synth/cursor.
    auto worker=std::async(std::launch::async,[] { return loadSoundFontBanks("Cache.sflist"); });
    const auto concurrent=loadSoundFontBanks("Cache.sflist"), background=worker.get();
    require(concurrent[0].bytes.constData()==background[0].bytes.constData(),"Concurrent cache readers lost shared input storage.");

    write(listPath,"CacheSecond.sf2\nCacheFirst.sf2\n");
    const auto reordered=loadSoundFontBanks("Cache.sflist");
    require(reordered[0].name=="CacheSecond.sf2" && reordered[1].name=="CacheFirst.sf2" &&
            reordered[0].bytes.constData()==original[1].bytes.constData(),"Cached SFLIST ignored new ordering or reread unchanged banks.");

    // Same-size replacement: force a distinct file timestamp even on coarse filesystems.
    const auto oldTime=QFileInfo(firstPath).lastModified();
    auto changed=fixture; changed[changed.size()-1]^=1;
    write(firstPath,changed);
    QFile file(firstPath);
    require(file.open(QIODevice::ReadWrite) && file.setFileTime(oldTime.addSecs(2),QFileDevice::FileModificationTime),
            "Cannot date changed SoundFont fixture.");
    file.close();
    const auto replacement=loadSoundFontBanks("Cache.sflist");
    require(replacement[1].bytes==changed && replacement[1].key!=original[0].key && original[0].bytes==fixture &&
            replacement[0].bytes.constData()==original[1].bytes.constData(),"Changed bank was stale or changed an existing snapshot.");

    require(QFile::remove(firstPath),"Cannot remove cached bank fixture.");
    require(rejects("Cache.sflist") && rejects("CacheFirst.sf2"),"Cache concealed a missing bank.");
    write(firstPath,"Invalid SoundFont");
    require(rejects("Cache.sflist"),"Cache concealed an invalid replacement bank.");
    write(firstPath,fixture);
    const auto beforeRefresh=loadSoundFontBanks("Cache.sflist");
    clearSoundFontBankCache();
    const auto afterRefresh=loadSoundFontBanks("Cache.sflist");
    require(afterRefresh[0].bytes==beforeRefresh[0].bytes &&
            afterRefresh[0].bytes.constData()!=beforeRefresh[0].bytes.constData(),"Refresh did not discard retained bank input.");

    loadSoundFontBanks("CacheFirst.sf2");
    const auto afterOtherSet=loadSoundFontBanks("Cache.sflist");
    require(afterOtherSet[0].bytes.constData()!=afterRefresh[0].bytes.constData(),"Cache retained banks outside the last loaded set.");
    clearSoundFontBankCache();
    require(QFile::remove(firstPath) && QFile::remove(secondPath) && QFile::remove(listPath),"Cannot clean SoundFont cache fixtures.");
    log << "SoundFont cache: shared/concurrent reads, changed bytes and list order, missing/invalid banks, refresh and bounded retention PASS\n";
    log.flush();
}

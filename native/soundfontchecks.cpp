#include "soundfontbank.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
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

#include "playlist.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStringDecoder>
#include <QUrl>
#include <algorithm>
#include <stdexcept>

namespace {
void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
constexpr int MaxEntries = 100000;
}
void Playlist::clear() { entries.clear(); current = successor_ = 0; history_.clear(); historyAt_ = -1; visited_.clear(); }
int Playlist::row(quint64 id) const {
    for (int i=0; i<entries.size(); ++i) if (entries[i].id==id) return i;
    return -1;
}
void Playlist::append(QVector<PlaylistEntry> items) {
    if(entries.size()+items.size()>MaxEntries) fail("Playlist has too many entries.");
    for (auto &entry:items) {
        entry.id=++serial_;
        if(successor_==~quint64(0)) successor_=entry.id;
        entries.push_back(std::move(entry));
    }
}
void Playlist::activate(int at) {
    if (at<0 || at>=entries.size()) return;
    current=entries[at].id; successor_=0; visited_.insert(current);
    if (historyAt_>=0 && history_.value(historyAt_)==current) return;
    history_.resize(historyAt_+1); history_.push_back(current); historyAt_=history_.size()-1;
    if (history_.size()>4096) { history_.removeFirst(); --historyAt_; }
}
void Playlist::setShuffle(bool enabled) {
    shuffle=enabled; visited_.clear(); history_.clear(); historyAt_=-1;
    if (currentRow()>=0) { visited_.insert(current); history_.push_back(current); historyAt_=0; }
}
bool Playlist::hasNext(bool repeatAll) const {
    if (entries.isEmpty()) return false;
    if (repeatAll || row(successor_)>=0) return true;
    if (!shuffle && successor_==~quint64(0)) return false;
    if (!shuffle) return currentRow()<entries.size()-1;
    if (historyAt_+1<history_.size()) return true;
    for (const auto &entry:entries) if (!visited_.contains(entry.id)) return true;
    return false;
}
int Playlist::next(bool repeatAll) {
    if (entries.isEmpty()) return -1;
    if (shuffle) {
        if (historyAt_+1<history_.size()) { current=history_[++historyAt_]; return currentRow(); }
        QVector<int> candidates;
        for (int i=0;i<entries.size();++i) if (!visited_.contains(entries[i].id)) candidates.push_back(i);
        if (candidates.isEmpty() && repeatAll) {
            visited_.clear();
            for (int i=0;i<entries.size();++i) if (entries[i].id!=current || entries.size()==1) candidates.push_back(i);
            // The boundary track belongs to the preceding cycle, not this new one.
        }
        if (candidates.isEmpty()) return -1;
        const auto at=candidates[QRandomGenerator::global()->bounded(int(candidates.size()))];
        activate(at); return at;
    }
    int at=row(successor_);
    if (successor_==~quint64(0)) { if(!repeatAll)return -1; at=0; }
    if (at<0) at=currentRow()+1;
    if (at>=entries.size()) { if (!repeatAll) return -1; at=0; }
    activate(at); return at;
}
int Playlist::previous() {
    if (entries.isEmpty()) return -1;
    if (shuffle && currentRow()<0 && historyAt_>=0) { current=history_[historyAt_]; return currentRow(); }
    if (shuffle && historyAt_>0) { current=history_[--historyAt_]; return currentRow(); }
    if (shuffle) return currentRow();
    if (currentRow()<0) {
        const int at=successor_==~quint64(0)?int(entries.size())-1:std::max(0,row(successor_)-1);
        activate(at);return at;
    }
    const int at=std::max(0,currentRow()-1); activate(at); return at;
}
void Playlist::remove(const QSet<quint64> &ids) {
    const int playing=currentRow();
    const bool removingSuccessor=ids.contains(successor_);
    if (ids.contains(current)) {
        successor_=0;
        for (int i=playing+1;i<entries.size();++i) if (!ids.contains(entries[i].id)) { successor_=entries[i].id; break; }
    } else if (ids.contains(successor_)) {
        const int at=row(successor_); successor_=0;
        for(int i=at+1;i<entries.size();++i) if(!ids.contains(entries[i].id)) {successor_=entries[i].id;break;}
    }
    // A detached final track must stop, not unexpectedly return to the first.
    const bool detachedEnd=(ids.contains(current) || (playing<0 && removingSuccessor)) && !successor_;
    int before=0;
    for(int i=0;i<=historyAt_ && i<history_.size();++i) if(!ids.contains(history_[i])) ++before;
    history_.erase(std::remove_if(history_.begin(),history_.end(),[&](quint64 id){return ids.contains(id);}),history_.end());
    historyAt_=before-1;
    entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const auto &e){return ids.contains(e.id);}),entries.end());
    for(auto id:ids) visited_.remove(id);
    if(detachedEnd) successor_=~quint64(0);
}

bool isPlaylistFile(const QString &path) { const auto suffix=QFileInfo(path).suffix().toLower(); return suffix=="m3u" || suffix=="m3u8"; }
QVector<PlaylistEntry> readPlaylistFile(const QString &path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) fail("Could not open playlist: "+file.errorString());
    if(file.size()>16*1024*1024) fail("Playlist exceeds 16 MiB.");
    const auto bytes=file.readAll(); QStringDecoder decoder(QStringDecoder::Utf8);
    QString text=decoder(bytes);
    if(decoder.hasError()) {
        if(QFileInfo(path).suffix().compare("m3u8",Qt::CaseInsensitive)==0) fail("M3U8 must use UTF-8 text.");
        text=QString::fromLocal8Bit(bytes);
    }
    if(text.startsWith(QChar(0xfeff))) text.remove(0,1);
    const QDir base=QFileInfo(path).absoluteDir(); QVector<PlaylistEntry> entries;
    int song=-1; QString title;
    for(auto line:text.split('\n')) {
        line=line.trimmed(); if(line.isEmpty()) continue;
        if(line.startsWith("#EXTBMV-SUBSONG:")) {
            bool ok=false; const int n=line.mid(16).toInt(&ok);
            if(!ok || n<1 || n>MaxEntries) fail("Invalid playlist subsong number.");
            song=n-1; continue;
        }
        if(line.startsWith("#EXTINF:")) { const auto comma=line.indexOf(','); title=comma<0?QString():line.mid(comma+1); continue; }
        if(line.startsWith('#')) continue;
        if(entries.size()>=MaxEntries) fail("Playlist has too many entries.");
        // Only local paths and file: URLs. No network fetch or recursive playlists.
        if(line.startsWith("file:",Qt::CaseInsensitive)) {
            const auto url=QUrl(line); if(!url.isLocalFile()) fail("Invalid local file URL."); line=url.toLocalFile();
        } else if(line.contains("://")) fail("Only local music files are supported in playlists.");
#ifndef Q_OS_WIN
        line.replace('\\','/');
#endif
        const auto resolved=QDir::cleanPath(base.absoluteFilePath(line));
        if(isPlaylistFile(resolved)) fail("Nested playlists are not supported.");
        entries.push_back({0,resolved,title,{},song,1,-1}); song=-1;title.clear();
    }
    return entries;
}
void writePlaylistFile(const QString &path,const QVector<PlaylistEntry> &entries) {
    if(!isPlaylistFile(path)) fail("Use an .m3u8 or .m3u filename for the playlist.");
    QByteArray bytes="#EXTM3U\n"; const QDir base=QFileInfo(path).absoluteDir();
    for(const auto &entry:entries) {
        QString title=entry.title; title.replace('\n',' ');title.replace('\r',' ');
        QString relative=QDir::fromNativeSeparators(base.relativeFilePath(QFileInfo(entry.path).absoluteFilePath()));
        if(relative.contains('\n') || relative.contains('\r')) fail("A music filename contains a line break.");
        if(relative.startsWith('#')) relative.prepend("./");
        bytes+="#EXTINF:"+QByteArray::number(entry.durationMs<0?-1:entry.durationMs/1000)+","+title.toUtf8()+"\n";
        bytes+="#EXTBMV-SUBSONG:"+QByteArray::number(entry.song+1)+"\n"+relative.toUtf8()+"\n";
    }
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) fail("Could not save playlist: "+file.errorString());
}

PlaylistLoader::PlaylistLoader() {start(QThread::LowPriority);}
PlaylistLoader::~PlaylistLoader() {
    {std::lock_guard lock(mutex_);closing_=true;++revision_;requests_.clear();} wake_.notify_all();wait();
}
quint64 PlaylistLoader::cancel() {
    std::lock_guard lock(mutex_);++revision_;outstanding_-=int(requests_.size());requests_.clear();results_.clear();return revision_;
}
quint64 PlaylistLoader::add(const QStringList &paths) {
    std::lock_guard lock(mutex_);requests_.push_back({revision_,paths});++outstanding_;wake_.notify_all();return revision_;
}
QVector<PlaylistLoader::Result> PlaylistLoader::takeResults() {std::lock_guard lock(mutex_);QVector<Result> result;result.swap(results_);return result;}
void PlaylistLoader::run() {
    for(;;) {
        Request request;
        {std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return closing_ || !requests_.empty();});if(closing_)return;request=std::move(requests_.front());requests_.pop_front();}
        Result result{request.token,{},{}};
        try {
            QVector<PlaylistEntry> inputs;
            for(const auto &path:request.paths) {
                if(isPlaylistFile(path)) inputs+=readPlaylistFile(path);
                else inputs.push_back({0,QFileInfo(path).absoluteFilePath(),{},{},-1,1,-1});
                if(inputs.size()>MaxEntries) fail("Playlist has too many entries.");
            }
            QHash<QString,QVector<SongInfo>> cache; QHash<QString,QString> errors;
            for(const auto &input:inputs) {
                if(request.token!=revision_) break;
                if(!cache.contains(input.path) && !errors.contains(input.path)) {
                    try {const auto track=openMusicTrack(input.path);cache.insert(input.path,track->info().playlist);}
                    catch(const std::exception &e) {errors.insert(input.path,QString::fromUtf8(e.what()));}
                }
                const auto songs=cache.value(input.path);
                if(songs.isEmpty() || input.song>=songs.size()) {
                    auto entry=input;entry.song=std::max(0,entry.song);entry.title=entry.title.isEmpty()?QFileInfo(entry.path).completeBaseName():entry.title;
                    entry.error=errors.value(input.path,"Subsong is not available.");result.entries.push_back(entry);continue;
                }
                const int first=input.song<0?0:input.song, end=input.song<0?songs.size():input.song+1;
                for(int i=first;i<end;++i) result.entries.push_back({0,input.path,input.title.isEmpty() || (input.song<0 && songs.size()>1)?songs[i].title:input.title,{},i,int(songs.size()),songs[i].durationMs});
                if(result.entries.size()>MaxEntries) fail("Playlist has too many entries.");
            }
        } catch(const std::exception &e) {result.entries.clear();result.error=QString::fromUtf8(e.what());}
        {std::lock_guard lock(mutex_);if(request.token==revision_)results_.push_back(std::move(result));--outstanding_;}
    }
}

#pragma once
#include "engine.h"
#include <QSet>
#include <deque>

struct PlaylistEntry {
    quint64 id = 0;
    QString path, title, error;
    int song = 0, songs = 1, durationMs = -1;
};
enum class PlaylistRepeat { Off, Track, All };

// GUI-owned order and stable identities, independent of decoder generations.
class Playlist {
public:
    QVector<PlaylistEntry> entries;
    quint64 current = 0;
    bool shuffle = false;
    void clear();
    void append(QVector<PlaylistEntry> items);
    int row(quint64 id) const;
    int currentRow() const { return row(current); }
    void activate(int row);
    int next(bool repeatAll);
    int previous();
    bool hasNext(bool repeatAll) const;
    void setShuffle(bool enabled);
    void remove(const QSet<quint64> &ids);
private:
    quint64 serial_ = 0, successor_ = 0;
    QVector<quint64> history_;
    int historyAt_ = -1;
    QSet<quint64> visited_;
};

bool isPlaylistFile(const QString &path);
QVector<PlaylistEntry> readPlaylistFile(const QString &path); // song=-1 expands all subsongs.
void writePlaylistFile(const QString &path, const QVector<PlaylistEntry> &entries);

// One bounded background reader; no decoder is shared with playback or scopes.
class PlaylistLoader : public QThread {
public:
    struct Result { quint64 token; QVector<PlaylistEntry> entries; QString error; };
    PlaylistLoader();
    ~PlaylistLoader() override;
    quint64 cancel();
    quint64 add(const QStringList &paths);
    QVector<Result> takeResults();
    bool busy() const { return outstanding_.load() > 0; }
private:
    void run() override;
    struct Request { quint64 token; QStringList paths; };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Request> requests_;
    QVector<Result> results_;
    std::atomic<quint64> revision_{0};
    std::atomic<int> outstanding_{0};
    bool closing_ = false;
};

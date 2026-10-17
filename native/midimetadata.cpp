#include "midimetadata.h"
#include "engine.h"
#include <QFileInfo>
#include <algorithm>
#include <iterator>

QString midiTrackTitle(const MidiSequence &sequence, const QString &path) {
    return sequence.title.isEmpty() ? QFileInfo(path).completeBaseName() : sequence.title;
}
int midiTrackDurationMs(const MidiSequence &sequence) {
    constexpr int TailSeconds = 2;
    return int((sequence.endFrame + TailSeconds * SampleRate) * 1000 / SampleRate);
}
QVector<SongInfo> midiSongList(const QVector<MidiSequence> &songs, const QString &path) {
    QVector<SongInfo> result;
    for (int i = 0; i < songs.size(); ++i) {
        const auto title = songs[i].title.isEmpty() && songs.size() > 1 ? QString("Pattern %1").arg(i+1) : midiTrackTitle(songs[i], path);
        result.push_back({title, midiTrackDurationMs(songs[i]), 0});
    }
    return result;
}
void midiSongInfo(TrackInfo &info, const MidiSequence &sequence, const QVector<SongInfo> &songs, int song, bool playlist) {
    info.song = song; info.songs = std::max(1, int(songs.size()));
    info.title = songs.isEmpty() ? midiTrackTitle(sequence, info.path) : songs[song].title;
    info.durationMs = midiTrackDurationMs(sequence);
    if (playlist) info.playlist = songs.isEmpty() ? QVector<SongInfo>{{info.title, info.durationMs, 0}} : songs;
}
void MidiInstrumentHistory::remember(qint64 frame, const QStringList &names) {
    if (states_.empty() || names != states_.back().names) states_.push_back({frame, names});
    // Prune on unchanged labels too; retain the preceding state at the strict cutoff.
    while (states_.size() > 1 && states_[1].frame < frame - keepFrames_) states_.pop_front();
}
QStringList MidiInstrumentHistory::at(qint64 frame) const {
    const auto at = std::upper_bound(states_.begin(), states_.end(), frame,
        [](qint64 f, const State &state) { return f < state.frame; });
    return at == states_.begin() ? QStringList{} : std::prev(at)->names;
}

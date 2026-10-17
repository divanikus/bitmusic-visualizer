#pragma once
#include "soundconfig.h"
#include "voicemask.h"
#include "musicevents.h"
#include <QByteArray>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <array>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include "scopepacing.h"
struct Music_Emu;
struct BmTaps;
struct BmNotes;
class OutputHistory;
struct SidRoms;
struct YamahaRoms;
struct MidiResources;
struct Mt32Resources;
QByteArray readMusicFile(const QString &path);

constexpr int SampleRate = 44100;
constexpr int BlockFrames = 1024;
constexpr int ScopeFrames = 4096;

// Last output stage: interleaved L/R PCM, with no effect on emulation or scope history.
void applyOutputGain(short *samples, size_t count, float volume, uint32_t outputMask);

// One decoder producer and one real-time consumer. Reset only after sink reset.
class PcmRing {
public:
    static constexpr size_t Capacity = 32768;
    size_t size() const;
    bool push(const short *source, size_t samples);
    size_t pop(short *destination, size_t samples);
    void reset();
private:
    std::array<short, Capacity> samples_{};
    std::atomic<uint64_t> read_{0}, write_{0};
};

struct SongInfo {
    QString title;
    int durationMs = -1, loopStartMs = 0;
};
struct TrackInfo {
    std::shared_ptr<const NoteRoll> noteRoll;
    std::shared_ptr<const TrackerPatterns> patterns;
    std::shared_ptr<const SidRoms> sidRoms; // Same ROM bytes in audio and scopes, including cold seeks.
    std::shared_ptr<const YamahaRoms> yamahaRoms;
    std::shared_ptr<const MidiResources> midiResources;
    std::shared_ptr<const Mt32Resources> mt32Resources;
    SoundConfiguration configuration;
    QString path, title, system;
    QStringList voices;
    QVector<SongInfo> playlist;
    int loopStartMs = 0;
    int songs = 0, song = 0, durationMs = -1;
    bool stereoOutput = false;
    VoiceMask tonalMask = 0; // Sources with chip pitch metadata (possibly several tones).
    VoiceMask emptyVoices = 0; // Proven to contain no notes; initially hide their cards, never mute audio.
};

struct TrackBoundary {
    qint64 frame = -1; // First proven end/loop boundary, in output sample frames.
    qint64 loopFrame = -1; // -1 means restart from the beginning.
};

// A decoder instance belongs to exactly one worker. Audio and scopes never share it.
class MusicTrack {
public:
    virtual ~MusicTrack() = default;
    virtual const TrackInfo &info() const = 0;
    virtual bool seek(qint64 frame, const std::function<bool()> &cancelled = [] { return false; }) = 0;
    virtual void render(short *out, int frames) = 0;
    virtual void mute(VoiceMask mask) = 0;
    virtual void dry() = 0;
    virtual bool hasTaps() const = 0;
    virtual bool readTap(int channel, QVector<float> &left, QVector<float> &right) const = 0;
    virtual QVector<float> notePitches(int channel) const = 0;
    // Optional changing labels, queried at the audible playhead (not decoder read-ahead).
    virtual QStringList instrumentNames(qint64) const { return {}; }
    virtual PatternPosition patternPosition(qint64) const { return {}; }
    virtual qint64 frame() const = 0;
    virtual TrackBoundary boundary() const { return {}; }
};
std::unique_ptr<MusicTrack> openMusicTrack(const QString &path, int song = 0, bool readPlaylist = true, bool capture = false,
                                          std::shared_ptr<const SidRoms> roms = {}, std::shared_ptr<const YamahaRoms> yamahaRoms = {},
                                          std::shared_ptr<const MidiResources> midiResources = {}, std::shared_ptr<const Mt32Resources> mt32Resources = {});

class GmeTrack : public MusicTrack {
public:
    GmeTrack(const QString &path, int song = 0, bool readPlaylist = true, bool capture = false, QByteArray data = {});
    ~GmeTrack();
    GmeTrack(const GmeTrack &) = delete;
    GmeTrack &operator=(const GmeTrack &) = delete;
    const TrackInfo &info() const { return info_; }
    bool seek(qint64 frame, const std::function<bool()> &cancelled = [] { return false; });
    void render(short *out, int frames);
    void mute(VoiceMask mask);
    void dry();
    bool hasTaps() const { return taps_ != nullptr; }
    bool readTap(int channel, QVector<float>& left, QVector<float>& right) const;
    float noteHz(int channel) const; // -1 unavailable, 0 gated off.
    QVector<float> notePitches(int channel) const; // Active Hz, empty = silent, {-1} = unavailable.
    qint64 frame() const { return frame_; }
    TrackBoundary boundary() const override;
private:
    void readMetadata(bool readPlaylist);
    void release();
    TrackBoundary boundary_;
    Music_Emu *emu_ = nullptr;
    BmTaps *taps_ = nullptr;
    BmNotes *notes_ = nullptr;
    TrackInfo info_;
    qint64 frame_ = 0;
    VoiceMask muteMask_ = 0;
    bool dry_ = false;
    int psgNoteChannel_ = -1;
};

struct PlayerState {
    PatternPosition pattern;
    TrackInfo info;
    quint64 generation = 0;
    bool valid = false, busy = false, playing = false;
    bool finished = false; // Natural endpoint only; pause/Stop/seek never emit it.
    bool repeat = false;
    int positionMs = 0, playbackEndMs = -1;
    bool durationLimited = false;
    VoiceMask muteMask = 0;
    int soloChannel = -1; // Temporary exclusive listening; not a saved preference.
    uint32_t outputMask = 3; // Bit 0: left speaker, bit 1: right speaker.
    uint64_t starvations = 0, outputErrors = 0;
    uint64_t scopeBlocks = 0;
    bool sharedScopes = false;
    bool scopesSuspended = true;
    QString error, scopeError;
    QStringList instruments;
};
struct ScopeFrame {
    void retainChannels(VoiceMask visible);
    quint64 generation = 0;
    int positionMs = 0;
    QVector<QVector<float>> channels;
    VoiceMask mask = 0; // Channels ready at positionMs; preparing/hidden channels have empty rows.
    QVector<QVector<float>> left, right;
    QVector<QVector<float>> noteHz; // Independent list of active pitches for each source.
    QStringList instruments;
};
struct OutputFrame {
    quint64 generation = 0, token = 0, sequence = 0;
    int positionMs = 0;
    QVector<float> left, right;
};

class NativePlayer : public QThread {
public:
    NativePlayer();
    ~NativePlayer() override;
    void load(const QString &path, int song = 0);
    void unload();
    void setAutoSubsong(bool enabled) { autoSubsong_.store(enabled); }
    void seek(int milliseconds, bool playing);
    void setMuteMask(VoiceMask mask);
    void toggleSolo(int channel);
    void setVolume(float value) { volume_.store(value); }
    void setOutputMask(uint32_t mask) { outputMask_.store(mask & 3u); }
    void setRepeat(bool enabled) { repeat_.store(enabled); }
    void setUnknownDurationLimit(int ms) { unknownDurationLimit_.store(std::max(0, ms)); }
    int unknownDurationLimit() const { return unknownDurationLimit_.load(); }
    void setScopesEnabled(bool enabled);
    void setScopeMask(VoiceMask mask);
    void setScopeFrameRate(int fps);
    int scopeFrameRate() const { return scopeFrameRate_.load(); }
    void setOutputScopesEnabled(bool enabled);
    PlayerState state() const;
    ScopeFrame scopes() const;
    OutputFrame outputScopes() const;
    void shutdown();
private:
    class AudioWorker;
    class ScopeWorker;
    struct Command { quint64 generation; QString path; int song, ms; bool load, playing; bool unload = false; };
    static int playbackEndMs(qint64 frame);
    void submit(Command command);
    void run() override;
    void scopeLoop();
    mutable std::mutex stateMutex_, scopeMutex_;
    std::mutex scopeWaitMutex_;
    std::condition_variable scopeWake_;
    PlayerState state_;
    VoiceMask beforeSoloMask_ = 0; // Guarded with state_.soloChannel by stateMutex_.
    ScopeFrame scope_;
    std::optional<Command> pending_;
    PcmRing ring_;
    std::unique_ptr<OutputHistory> outputHistory_;
    OutputFrame pausedOutput_;
    std::atomic<quint64> outputToken_{0}; // Low bit enables capture; upper bits invalidate old history.
    std::atomic<quint64> generation_{0};
    std::atomic<bool> enabled_{false}, closing_{false};
    std::atomic<bool> repeat_{false}, durationLimited_{false};
    std::atomic<int> unknownDurationLimit_{0};
    std::atomic<bool> autoSubsong_{true};
    std::atomic<bool> scopesEnabled_{false}, scopesSuspended_{true};
    std::atomic<quint64> scopeRevision_{0};
    std::atomic<int> scopeFrameRate_{ScopePacing::DefaultFps};
    std::atomic<uint64_t> scopeBlocks_{0};
    std::atomic<bool> sharedScopes_{false};
    std::atomic<qint64> playedFrame_{0}, endFrame_{-1};
    SharedVoiceMask muteMask_{0};
    std::atomic<uint32_t> outputMask_{3};
    SharedVoiceMask scopeMask_{~VoiceMask{}};
    std::atomic<float> volume_{0.65f};
    std::atomic<uint64_t> starvations_{0}, outputErrors_{0};
};

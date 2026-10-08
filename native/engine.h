#pragma once
#include <QByteArray>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <gme.h>
#include "scopepacing.h"
struct BmTaps;
struct BmNotes;
class OutputHistory;

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
    size_t free() const { return Capacity - size(); }
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
    QString path, title, system;
    QStringList voices;
    QVector<SongInfo> playlist;
    int loopStartMs = 0;
    int songs = 0, song = 0, durationMs = -1;
    bool stereoOutput = false;
    uint32_t tonalMask = 0; // Sources with chip pitch metadata (possibly several tones).
};

class GmeTrack {
public:
    GmeTrack(const QString &path, int song = 0, bool readPlaylist = true, bool capture = false);
    ~GmeTrack();
    GmeTrack(const GmeTrack &) = delete;
    GmeTrack &operator=(const GmeTrack &) = delete;
    const TrackInfo &info() const { return info_; }
    bool seek(qint64 frame, const std::function<bool()> &cancelled = [] { return false; });
    void render(short *out, int frames);
    void mute(uint32_t mask);
    void dry();
    bool hasTaps() const { return taps_ != nullptr; }
    bool readTap(int channel, QVector<float>& left, QVector<float>& right) const;
    float noteHz(int channel) const; // -1 unavailable, 0 gated off.
    QVector<float> notePitches(int channel) const; // Active Hz, empty = silent, {-1} = unavailable.
    qint64 frame() const { return frame_; }
private:
    Music_Emu *emu_ = nullptr;
    BmTaps *taps_ = nullptr;
    BmNotes *notes_ = nullptr;
    TrackInfo info_;
    qint64 frame_ = 0;
    uint32_t muteMask_ = 0;
    bool dry_ = false;
    int psgNoteChannel_ = -1;
};

struct PlayerState {
    TrackInfo info;
    quint64 generation = 0;
    bool valid = false, busy = false, playing = false;
    bool repeat = false;
    int positionMs = 0;
    uint32_t muteMask = 0;
    uint32_t outputMask = 3; // Bit 0: left speaker, bit 1: right speaker.
    uint64_t starvations = 0, outputErrors = 0;
    uint64_t scopeBlocks = 0;
    bool sharedScopes = false;
    bool scopesSuspended = true;
    QString error, scopeError;
};
struct ScopeFrame {
    quint64 generation = 0;
    int positionMs = 0;
    QVector<QVector<float>> channels;
    uint32_t mask = 0; // Channels ready at positionMs; preparing/hidden channels have empty rows.
    QVector<QVector<float>> left, right;
    QVector<QVector<float>> noteHz; // Independent list of active pitches for each source.
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
    void seek(int milliseconds, bool playing);
    void setMuteMask(uint32_t mask) { muteMask_.store(mask); }
    void setVolume(float value) { volume_.store(value); }
    void setOutputMask(uint32_t mask) { outputMask_.store(mask & 3u); }
    void setRepeat(bool enabled) { repeat_.store(enabled); }
    void setScopesEnabled(bool enabled);
    void setScopeMask(uint32_t mask);
    void setScopeFrameRate(int fps);
    int scopeFrameRate() const { return scopeFrameRate_.load(); }
    void setOutputScopesEnabled(bool enabled);
    PlayerState state() const;
    ScopeFrame scopes() const;
    OutputFrame outputScopes() const;
    void shutdown();
private:
    struct Command { quint64 generation; QString path; int song, ms; bool load, playing; };
    void submit(Command command);
    void run() override;
    void scopeLoop();
    mutable std::mutex stateMutex_, scopeMutex_;
    std::mutex scopeWaitMutex_;
    std::condition_variable scopeWake_;
    PlayerState state_;
    ScopeFrame scope_;
    std::optional<Command> pending_;
    PcmRing ring_;
    std::unique_ptr<OutputHistory> outputHistory_;
    OutputFrame pausedOutput_;
    std::atomic<quint64> outputToken_{0}; // Low bit enables capture; upper bits invalidate old history.
    std::atomic<quint64> generation_{0};
    std::atomic<bool> enabled_{false}, closing_{false};
    std::atomic<bool> repeat_{false};
    std::atomic<bool> scopesEnabled_{false}, scopesSuspended_{true};
    std::atomic<quint64> scopeRevision_{0};
    std::atomic<int> scopeFrameRate_{ScopePacing::DefaultFps};
    std::atomic<uint64_t> scopeBlocks_{0};
    std::atomic<bool> sharedScopes_{false};
    std::atomic<qint64> playedFrame_{0}, endFrame_{-1};
    std::atomic<uint32_t> muteMask_{0};
    std::atomic<uint32_t> outputMask_{3};
    std::atomic<uint32_t> scopeMask_{0xffffffffu};
    std::atomic<float> volume_{0.65f};
    std::atomic<uint64_t> starvations_{0}, outputErrors_{0};
};

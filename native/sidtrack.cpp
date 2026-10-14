#include "sidtrack.h"
#include "sid-taps/taps.h"
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/SidInfo.h>
#include <residfp.h>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <algorithm>
#include <stdexcept>

namespace {
void requireSid(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
}

struct SidTrack::Impl {
    // Destruction order matters: player releases chips before builder/tune.
    BmSidTaps taps;
    ReSIDfpBuilder builder{"Bit Music Visualizer"};
    std::unique_ptr<SidTune> tune;
    sidplayfp player;
    std::array<short, 2048> pending{};
    int first = 0, end = 0;
    QByteArray data;
    void start(const QByteArray &bytes, int song, bool capture) {
        data = bytes;
        BmSidScope scope(&taps);
        tune = std::make_unique<SidTune>(reinterpret_cast<const uint8_t *>(data.constData()), uint32_t(data.size()));
        requireSid(tune->getStatus(), tune->statusString());
        const auto meta = tune->getInfo();
        requireSid(meta->sidChips() == 1, "This version supports one SID chip; multi-SID files are not supported yet.");
        requireSid(song >= 0 && song < int(meta->songs()), "Invalid subsong number.");
        requireSid(!meta->fixLoad(), "SID contains a duplicate load address; repair the source file outside the player.");
        tune->selectSong(unsigned(song + 1));
        builder.create(1);
        requireSid(builder.getStatus(), builder.error());
        auto cfg = player.config();
        cfg.sidEmulation = &builder;
        cfg.frequency = SampleRate;
        cfg.playback = SidConfig::MONO;
        cfg.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
        cfg.powerOnDelay = 0; // Deterministic audio/scope instances.
        taps.capture = capture;
        if (!player.config(cfg)) throw std::runtime_error(player.error());
        if (!player.load(tune.get())) throw std::runtime_error(player.error());
        // Boot PCM was discarded upstream; preserve resampler phase, reset history origin.
        taps.clear();
    }
};

SidTrack::SidTrack(const QString &path, int song, bool readPlaylist, bool capture)
    : impl_(std::make_unique<Impl>()) {
    QFile file(path);
    requireSid(file.open(QIODevice::ReadOnly), "Cannot open SID file.");
    auto data = file.read(1024 * 1024 + 1);
    requireSid(data.size() <= 1024 * 1024, "SID file exceeds 1 MB.");
    requireSid(!data.startsWith("RSID"), "RSID playback is not supported yet (C64 ROM-dependent execution).");
    requireSid(data.startsWith("PSID"), "Expected a PSID file.");
    auto &d = *impl_;
    d.start(data, song, capture);
    const auto meta = d.tune->getInfo();
    info_.path = path; info_.song = song; info_.songs = int(meta->songs());
    info_.system = "Commodore 64";
    info_.voices = {"Voice 1", "Voice 2", "Voice 3"};
    const QString title = QString::fromLatin1(meta->infoString(0)).trimmed();
    const auto base = title.isEmpty() ? QFileInfo(path).completeBaseName() : title;
    for (int i = 0; i < info_.songs; ++i) {
        const auto name = info_.songs == 1 ? base : base + QString(" / Track %1").arg(i+1, 2, 10, QLatin1Char('0'));
        if (readPlaylist) info_.playlist.push_back({name, -1, 0});
        if (i == song) info_.title = name;
    }
    // Describe the model actually selected by the backend; unknown/any uses 6581.
    const bool newer = d.player.info().sidModel(0) == SidTuneInfo::SIDMODEL_8580;
    auto &c = info_.configuration;
    c.family = "sid"; c.name = newer ? "SID 8580" : "SID 6581";
    c.sources = {"sid.voice:0", "sid.voice:1", "sid.voice:2"};
    c.id = QString::fromLatin1(QCryptographicHash::hash((c.family+'|'+c.name).toUtf8(), QCryptographicHash::Sha256).toHex());
}
SidTrack::~SidTrack() = default;
bool SidTrack::hasTaps() const { return impl_->taps.capture; }
void SidTrack::mute(uint32_t mask) { impl_->taps.muteMask = mask & 7u; }
void SidTrack::render(short *out, int frames) {
    auto &d = *impl_; BmSidScope scope(&d.taps);
    while (frames > 0) {
        if (d.first == d.end) {
            // Bounded emulation batches: much smaller than libsidplayfp's 20ms
            // chip buffer, with at most a few milliseconds of lookahead.
            const int count = d.player.play(1000u);
            requireSid(count > 0 && count <= int(d.pending.size()), d.player.error());
            short *buffers[3]{}; d.player.buffers(buffers);
            std::copy_n(buffers[0], count, d.pending.data());
            d.first = 0; d.end = count;
        }
        const int count = std::min(frames, d.end-d.first);
        for (int i = 0; i < count; ++i) { const auto v = d.pending[d.first++]; *out++ = v; *out++ = v; }
        frames -= count; frame_ += count;
    }
}
bool SidTrack::seek(qint64 target, const std::function<bool()> &cancelled) {
    target = std::clamp<qint64>(target, 0, 3600LL * SampleRate);
    if (cancelled()) return false;
    if (target < frame_) {
        // A hardware SID reset deliberately retains oscillator/envelope state.
        // Seeking needs a cold chip, otherwise audio and scopes acquire different phases.
        auto fresh = std::make_unique<Impl>();
        fresh->start(impl_->data, info_.song, impl_->taps.capture);
        fresh->taps.muteMask = impl_->taps.muteMask;
        impl_ = std::move(fresh); frame_ = 0;
    }
    std::array<short, BlockFrames * 2> scratch;
    while (frame_ < target) {
        if (cancelled()) return false;
        render(scratch.data(), int(std::min<qint64>(BlockFrames, target-frame_)));
    }
    return !cancelled();
}
bool SidTrack::readTap(int channel, QVector<float> &left, QVector<float> &right) const {
    const auto &t = impl_->taps;
    if (!t.capture || channel < 0 || channel >= 3 || t.samples < frame_ || t.samples-frame_ > BmSidTaps::Capacity-ScopeFrames) return false;
    left.fill(0, ScopeFrames);
    for (int i = 0; i < ScopeFrames; ++i) {
        const auto at = frame_-ScopeFrames+i;
        if (at >= 0) left[i] = t.history[channel][at % BmSidTaps::Capacity];
    }
    right = left; return true;
}

std::unique_ptr<MusicTrack> openMusicTrack(const QString &path, int song, bool playlist, bool capture) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const auto magic = file.read(4);
        if (magic == "PSID" || magic == "RSID") return std::make_unique<SidTrack>(path, song, playlist, capture);
    }
    return std::make_unique<GmeTrack>(path, song, playlist, capture);
}

#include "sidtrack.h"
#include "pokeytrack.h"
#include "yamahatrack.h"
#include "vgm.h"
#include "sid-taps/taps.h"
#include "romfiles.h"
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/SidInfo.h>
#include <residfp.h>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
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
    std::shared_ptr<const SidRoms> roms;
    int chips = 1;
    void start(const QByteArray &bytes, int song, bool capture, std::shared_ptr<const SidRoms> resources) {
        roms = resources ? std::move(resources) : loadSidRoms();
        try {
            data = bytes;
            BmSidScope scope(&taps);
            tune = std::make_unique<SidTune>(reinterpret_cast<const uint8_t *>(data.constData()), uint32_t(data.size()));
            requireSid(tune->getStatus(), tune->statusString());
            const auto meta = tune->getInfo();
            chips = int(meta->sidChips());
            requireSid(chips >= 1 && chips <= 3, "Unsupported SID chip count.");
            requireSid(song >= 0 && song < int(meta->songs()), "Invalid subsong number.");
            requireSid(!meta->fixLoad(), "SID contains a duplicate load address; repair the source file outside the player.");
            tune->selectSong(unsigned(song + 1));
            builder.create(chips);
            requireSid(builder.getStatus(), builder.error());
            auto cfg = player.config();
            cfg.sidEmulation = &builder;
            cfg.frequency = SampleRate;
            cfg.playback = SidConfig::MONO;
            cfg.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
            cfg.powerOnDelay = 0; // Deterministic audio/scope instances.
            taps.capture = capture;
            auto rom = [&](int i) { return roms->bytes[i].isEmpty() ? nullptr : reinterpret_cast<const uint8_t *>(roms->bytes[i].constData()); };
            player.setRoms(rom(0), rom(1), rom(2));
            if (meta->compatibility() == SidTuneInfo::COMPATIBILITY_BASIC && !rom(1)) missingSidRom(*roms, 1);
            if (!player.config(cfg)) throw std::runtime_error(player.error());
            if (!player.load(tune.get())) throw std::runtime_error(player.error());
            requireSid(taps.count == chips, "SID capture chip mapping failed.");
            player.initMixer(true);
            // Boot PCM was discarded upstream; preserve resampler phase, reset history origin.
            taps.clear();
        } catch (const BmSidRomNeeded &needed) { missingSidRom(*roms, needed.index); }
    }
};

SidTrack::SidTrack(const QString &path, int song, bool readPlaylist, bool capture, std::shared_ptr<const SidRoms> roms)
    : impl_(std::make_unique<Impl>()) {
    QFile file(path);
    requireSid(file.open(QIODevice::ReadOnly), "Cannot open SID file.");
    auto data = file.read(1024 * 1024 + 1);
    requireSid(data.size() <= 1024 * 1024, "SID file exceeds 1 MB.");
    requireSid(data.startsWith("PSID") || data.startsWith("RSID"), "Expected a PSID or RSID file.");
    auto &d = *impl_;
    d.start(data, song, capture, std::move(roms));
    info_.sidRoms = d.roms;
    const auto meta = d.tune->getInfo();
    info_.path = path; info_.song = song; info_.songs = int(meta->songs());
    info_.system = "Commodore 64";
    info_.stereoOutput = d.chips > 1;
    for (int chip = 0; chip < d.chips; ++chip) for (int voice = 0; voice < 3; ++voice)
        info_.voices << (d.chips == 1 ? QString("Voice %1").arg(voice+1) : QString("SID %1 / Voice %2").arg(chip+1).arg(voice+1));
    const QString title = QString::fromLatin1(meta->infoString(0)).trimmed();
    const auto base = title.isEmpty() ? QFileInfo(path).completeBaseName() : title;
    for (int i = 0; i < info_.songs; ++i) {
        const auto name = info_.songs == 1 ? base : base + QString(" / Track %1").arg(i+1, 2, 10, QLatin1Char('0'));
        if (readPlaylist) info_.playlist.push_back({name, -1, 0});
        if (i == song) info_.title = name;
    }
    // Describe the model actually selected by the backend; unknown/any uses 6581.
    auto &c = info_.configuration;
    c.family = "sid";
    QStringList models, addresses;
    for (int chip = 0; chip < d.chips; ++chip) {
        models << (d.player.info().sidModel(chip) == SidTuneInfo::SIDMODEL_8580 ? "SID 8580" : "SID 6581");
        const auto address = QString::number(meta->sidChipBase(chip), 16);
        addresses << address;
        for (int voice = 0; voice < 3; ++voice)
            c.sources << (chip == 0 ? QString("sid.voice:%1").arg(voice) : QString("sid.%1.voice:%2").arg(address).arg(voice));
    }
    c.name = models.join(" + ");
    // Keep existing single-SID presets; additional chips are identified by address.
    const auto identity = c.family+'|'+c.name+(d.chips > 1 ? '|'+addresses.join(',') : QString());
    c.id = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
}
SidTrack::~SidTrack() = default;
bool SidTrack::hasTaps() const { return impl_->taps.capture; }
void SidTrack::mute(uint32_t mask) { impl_->taps.muteMask = mask & ((1u << (impl_->chips*3))-1); }
void SidTrack::render(short *out, int frames) {
    auto &d = *impl_; BmSidScope scope(&d.taps);
    try { while (frames > 0) {
        if (d.first == d.end) {
            // Bounded emulation batches: much smaller than libsidplayfp's 20ms
            // chip buffer, with at most a few milliseconds of lookahead.
            const int count = d.player.play(1000u);
            requireSid(count > 0 && count <= int(d.pending.size()/2), d.player.error());
            d.player.mix(d.pending.data(), unsigned(count));
            d.first = 0; d.end = count;
        }
        const int count = std::min(frames, d.end-d.first);
        std::copy_n(d.pending.data()+d.first*2, count*2, out); out += count*2; d.first += count;
        frames -= count; frame_ += count;
    }} catch (const BmSidRomNeeded &needed) { missingSidRom(*d.roms, needed.index); }
}
bool SidTrack::seek(qint64 target, const std::function<bool()> &cancelled) {
    target = std::clamp<qint64>(target, 0, 3600LL * SampleRate);
    if (cancelled()) return false;
    if (target < frame_) {
        // A hardware SID reset deliberately retains oscillator/envelope state.
        // Seeking needs a cold chip, otherwise audio and scopes acquire different phases.
        auto fresh = std::make_unique<Impl>();
        fresh->start(impl_->data, info_.song, impl_->taps.capture, impl_->roms);
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
    if (!impl_->taps.capture || channel < 0 || channel >= impl_->chips*3) return false;
    const int chip = channel/3;
    const auto &t = impl_->taps.chips[chip];
    if (t.samples < frame_ || t.samples-frame_ > BmSidTaps::Capacity-ScopeFrames) return false;
    // Same fixed chip pan as libsidplayfp's mixer; a SID itself remains mono.
    const float gain = 1.f/std::sqrt(float(impl_->chips));
    const float l = gain*(impl_->chips > 1 && chip == impl_->chips-1 ? .5f : 1.f);
    const float r = gain*(impl_->chips > 1 && chip == 0 ? .5f : 1.f);
    left.fill(0, ScopeFrames); right.fill(0, ScopeFrames);
    for (int i = 0; i < ScopeFrames; ++i) {
        const auto at = frame_-ScopeFrames+i;
        if (at >= 0) { const auto v = t.history[channel%3][at % BmSidTaps::Capacity]; left[i] = v*l; right[i] = v*r; }
    }
    return true;
}

std::unique_ptr<MusicTrack> openMusicTrack(const QString &path, int song, bool playlist, bool capture, std::shared_ptr<const SidRoms> roms) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const auto magic = file.read(4);
        if (magic == "PSID" || magic == "RSID") return std::make_unique<SidTrack>(path, song, playlist, capture, std::move(roms));
    }
    auto data = readMusicFile(path);
    if (data.startsWith("Vgm ")) {
        const auto header = inspectVgm(data); requireSupportedVgm(header);
        if (!header.yamaha.isEmpty()) return std::make_unique<YamahaTrack>(path, std::move(data), song, playlist, capture);
        if (header.pokeyClock) return std::make_unique<PokeyTrack>(path, std::move(data), song, playlist, capture);
    }
    return std::make_unique<GmeTrack>(path, song, playlist, capture, std::move(data));
}

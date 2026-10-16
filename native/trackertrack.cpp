#include "trackertrack.h"
#include "xmp-taps/capture.h"
#include <xmp.h>
#include <QCryptographicHash>
#include <QFileInfo>
#include <algorithm>
#include <array>
#include <deque>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
QString name(const char *s) { return QString::fromLatin1(s, int(qstrnlen(s, XMP_NAME_SIZE))).trimmed(); }
}
bool isTrackerModule(const QByteArray &data) {
    return xmp_test_module_from_memory(data.constData(), long(data.size()), nullptr) == 0;
}
struct TrackerTrack::Impl {
    xmp_context ctx = xmp_create_context();
    bool loaded = false, started = false, capture;
    int entry = 0, first = 0, end = 0;
    VoiceMask muteMask;
    std::unique_ptr<XmpCapture> taps;
    QVector<std::array<QVector<float>, 2>> history;
    xmp_module_info module{};
    xmp_frame_info tick{};
    struct Names { qint64 frame; QStringList channels; };
    std::deque<Names> names;
    explicit Impl(bool cap) : capture(cap) { require(ctx != nullptr, "Cannot create tracker decoder."); }
    ~Impl() {
        if (started) xmp_end_player(ctx);
        if (loaded) xmp_release_module(ctx);
        xmp_free_context(ctx);
    }
    void reset() {
        if (started) { xmp_end_player(ctx); started = false; }
        require(xmp_start_player(ctx, SampleRate, 0) == 0, "Cannot start tracker decoder.");
        started = true;
        require(xmp_set_player(ctx, XMP_PLAYER_MIX, 100) == 0, "Cannot configure tracker stereo.");
        require(xmp_set_player(ctx, XMP_PLAYER_CFLAGS,
            xmp_get_player(ctx, XMP_PLAYER_CFLAGS) | XMP_FLAGS_A500) == 0, "Cannot select Amiga mixer.");
        // libxmp returns -1 for a valid restart at order zero.
        require(xmp_set_position(ctx, entry) >= -1, "Cannot select tracker sequence.");
        for (int i=0; i<module.mod->chn; ++i) xmp_channel_mute(ctx, i, muteMask.test(i));
        first = end = 0; tick = {}; names.clear();
        for (auto &channel : history) for (auto &side : channel) side.fill(0, ScopeFrames);
    }
    void next(qint64 frame) {
        XmpCaptureScope scope(capture ? taps.get() : nullptr);
        require(xmp_play_frame(ctx) == 0, "Tracker decoding failed.");
        xmp_get_frame_info(ctx, &tick);
        require(tick.buffer && tick.buffer_size > 0 && tick.buffer_size % 4 == 0, "Invalid tracker audio block.");
        first = 0; end = tick.buffer_size/4;
        if (capture) require(taps->samples == end*2, "Tracker capture clock mismatch.");
        QStringList labels;
        for (int i=0; i<module.mod->chn; ++i) {
            const auto &ci = tick.channel_info[i];
            QString label;
            if (ci.volume && ci.period) {
                if (ci.instrument < module.mod->ins) label = name(module.mod->xxi[ci.instrument].name);
                if (label.isEmpty() && ci.sample < module.mod->smp) label = name(module.mod->xxs[ci.sample].name);
                if (label.isEmpty()) label = QString("Sample %1").arg(ci.sample+1);
            }
            labels << label;
        }
        if (names.empty() || names.back().channels != labels) names.push_back({frame, labels});
        // Keep enough metadata for the audio ring's read-ahead, with bounded memory.
        while (names.size()>1 && names[1].frame < frame-8*SampleRate) names.pop_front();
    }
};
TrackerTrack::TrackerTrack(const QString &path, QByteArray data, int song, bool playlist, bool capture)
    : impl_(std::make_unique<Impl>(capture)) {
    auto &d = *impl_;
    if (data.isEmpty()) data = readMusicFile(path);
    require(xmp_set_player(d.ctx, XMP_PLAYER_DEFPAN, 100) == 0, "Cannot configure tracker panning.");
    require(xmp_load_module_from_memory(d.ctx, data.constData(), long(data.size())) == 0,
            "Invalid or unsupported tracker module.");
    d.loaded = true;
    xmp_get_module_info(d.ctx, &d.module);
    const auto *m = d.module.mod;
    require(m && m->chn>0 && m->chn<=VoiceMask::Capacity, "Unsupported tracker channel count.");
    require(song>=0 && song<d.module.num_sequences, "Invalid tracker subsong.");
    d.entry = d.module.seq_data[song].entry_point;
    d.taps = std::make_unique<XmpCapture>(m->chn);
    if (capture) d.history.resize(m->chn);
    d.reset();
    const bool amiga = xmp_get_player(d.ctx, XMP_PLAYER_MIXER_TYPE) != XMP_MIXER_STANDARD;
    info_.path = path; info_.song = song; info_.songs = d.module.num_sequences;
    info_.stereoOutput = true;
    const bool amigaFormat = amiga || data.startsWith("MMD") || data.startsWith("MED") ||
        data.startsWith("DBM0") || data.startsWith("DIGI") || data.startsWith("OKTASONG");
    info_.system = amigaFormat ? "Amiga" : "Tracker";
    QString title = name(m->name);
    if (title.isEmpty()) title = QFileInfo(path).completeBaseName();
    for (int i=0; i<info_.songs; ++i) {
        SongInfo s;
        s.title = info_.songs==1 ? title : QString("%1 — %2").arg(title).arg(i+1);
        s.durationMs = d.module.seq_data[i].duration>0 ? d.module.seq_data[i].duration : -1;
        if (i==song) { info_.title=s.title; info_.durationMs=s.durationMs; }
        if (playlist) info_.playlist << s;
    }
    auto &c = info_.configuration;
    c.family = "tracker";
    c.name = amiga ? QString("Paula · %1 channels").arg(m->chn)
                   : QString("%1 · %2 channels").arg(name(m->type)).arg(m->chn);
    for (int i=0; i<m->chn; ++i) {
        info_.voices << QString("Channel %1").arg(i+1);
        c.sources << QString("tracker.channel:%1").arg(i);
    }
    // Layout depends on the mixer and channel count, not the exporting tracker's version.
    const QString identity = QString("tracker|%1|%2").arg(amiga ? "paula" : "sample-mixer").arg(m->chn);
    c.id = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
}
TrackerTrack::~TrackerTrack() = default;
void TrackerTrack::render(short *out, int frames) {
    auto &d = *impl_;
    while (frames>0) {
        if (d.first==d.end) d.next(frame_);
        const int count = std::min(frames, d.end-d.first);
        std::copy_n(static_cast<const short *>(d.tick.buffer)+2*d.first, count*2, out);
        if (d.capture) for (int c=0; c<d.module.mod->chn; ++c) for (int i=0; i<count; ++i) {
            const auto at = (frame_+i)%ScopeFrames;
            d.history[c][0][at] = d.taps->channels[c][2*(d.first+i)];
            d.history[c][1][at] = d.taps->channels[c][2*(d.first+i)+1];
        }
        out += count*2; frames -= count; d.first += count; frame_ += count;
    }
}
void TrackerTrack::mute(VoiceMask mask) {
    impl_->muteMask = mask;
    for (int i=0; i<info_.voices.size(); ++i) xmp_channel_mute(impl_->ctx, i, mask.test(i));
}
bool TrackerTrack::seek(qint64 target, const std::function<bool()> &cancelled) {
    target = std::clamp<qint64>(target, 0, info_.durationMs>0 ? qint64(info_.durationMs)*SampleRate/1000 : 3600LL*SampleRate);
    if (cancelled()) return false;
    if (target<frame_) { impl_->reset(); frame_=0; }
    std::array<short, BlockFrames*2> scratch;
    while (frame_<target) {
        if (cancelled()) return false;
        render(scratch.data(), int(std::min<qint64>(BlockFrames, target-frame_)));
    }
    return !cancelled();
}
bool TrackerTrack::hasTaps() const { return impl_->capture; }
bool TrackerTrack::readTap(int channel, QVector<float> &left, QVector<float> &right) const {
    if (!impl_->capture || channel<0 || channel>=info_.voices.size()) return false;
    left.fill(0, ScopeFrames); right.fill(0, ScopeFrames);
    for (int i=0; i<ScopeFrames; ++i) {
        const auto at = frame_-ScopeFrames+i;
        if (at>=0) { left[i]=impl_->history[channel][0][at%ScopeFrames]; right[i]=impl_->history[channel][1][at%ScopeFrames]; }
    }
    return true;
}
QStringList TrackerTrack::instrumentNames(qint64 frame) const {
    const auto &names = impl_->names;
    const auto at = std::upper_bound(names.begin(), names.end(), frame,
        [](qint64 f, const Impl::Names &n) { return f<n.frame; });
    return at==names.begin() ? QStringList{} : std::prev(at)->channels;
}

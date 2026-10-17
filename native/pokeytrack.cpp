#include "pokeytrack.h"
#include "vgm.h"
#include "gme-taps/pokey.h"
#include <QFileInfo>
#include <QtEndian>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
constexpr int Batch = 256;
}
struct PokeyTrack::Impl {
    struct Event { qint64 tick; int address, value; };
    std::unique_ptr<BmPokey, decltype(&bm_pokey_delete)> chip{nullptr, bm_pokey_delete};
    QVector<Event> events;
    qint64 ticks = 0, loopTick = 0, loopDuration = 0;
    qsizetype loopEvent = 0, next = 0;
    qint64 timeline = 0, iteration = 0;
    int clock = 0, first = 0, available = 0;
    bool capture = false, ended = false;
    std::array<short, 4*Batch> pending{};
    std::array<std::array<float, ScopeFrames>, 4> history{};
    void reset() {
        chip.reset(bm_pokey_create(clock, SampleRate));
        require(bool(chip), "Cannot allocate POKEY audio buffers.");
        next = 0; timeline = iteration = 0; first = available = 0; ended = false;
        for (auto &voice : history) voice.fill(0);
    }
    qint64 cycles(qint64 tick) const { return tick * clock / SampleRate; }
    void fill() {
        while (!(available = bm_pokey_read(chip.get(), pending.data(), Batch))) {
            const qint64 end = timeline + Batch, startClock = cycles(timeline);
            while (!ended) {
                if (next == events.size()) {
                    if (!loopDuration) {
                        if (ticks >= end) break;
                        for (int i = 0; i < 4; ++i) bm_pokey_write(chip.get(), int(cycles(ticks)-startClock), 2*i+1, 0);
                        ended = true; break;
                    }
                    if (iteration + ticks >= end) break;
                    iteration += loopDuration; next = loopEvent;
                }
                if (next == events.size()) break; // A silent loop containing only waits.
                const auto &e = events[next];
                const qint64 time = e.tick + iteration;
                if (time >= end) break;
                bm_pokey_write(chip.get(), int(cycles(time)-startClock), e.address, e.value);
                ++next;
            }
            bm_pokey_end(chip.get(), int(cycles(end)-startClock));
            timeline = end;
        }
        first = 0;
    }
};

PokeyTrack::PokeyTrack(const QString &path, QByteArray data, int song, bool playlist, bool capture)
    : impl_(std::make_unique<Impl>()) {
    require(song == 0, "Invalid subsong number.");
    const auto h = inspectVgm(data); requireSupportedVgm(h);
    auto word = [&](qsizetype at) { return qFromLittleEndian<quint32>(data.constData()+at); };
    auto &d = *impl_; d.capture = capture; d.clock = int(h.pokeyClock);
    require(d.clock >= 100000 && d.clock <= 5000000, "Unsupported POKEY clock rate.");
    const quint64 eof = 4ULL + word(4);
    require(eof == quint64(data.size()), "Truncated or inconsistent VGM file size.");
    const quint64 loopOffset = word(0x1c) ? 0x1cULL+word(0x1c) : 0;
    bool loopFound = false, endFound = false;
    int wait62 = 735, wait63 = 882;
    for (qsizetype pos = h.dataOffset; pos < data.size();) {
        if (quint64(pos) == loopOffset) {
            d.loopTick = d.ticks; d.loopEvent = d.events.size(); loopFound = true;
        }
        const auto op = quint8(data[pos++]);
        auto need = [&](int count) { require(data.size()-pos >= count, "Truncated POKEY VGM command."); };
        if (op == 0x66) { endFound = true; break; }
        if (op == 0xbb) {
            need(2); const int address = quint8(data[pos++]), value = quint8(data[pos++]);
            require(address < 16, "Unsupported POKEY register or second POKEY chip.");
            // SAP's core does not implement serial/two-tone modes or SKCTL reset.
            require(address != 15 || value == 3, "This POKEY SKCTL mode is not supported yet.");
            require(d.events.size() < 2000000, "POKEY VGM has too many register writes.");
            d.events.push_back({d.ticks, address, value});
        } else if (op == 0x61) { need(2); d.ticks += qFromLittleEndian<quint16>(data.constData()+pos); pos += 2; }
        else if (op == 0x62) d.ticks += wait62;
        else if (op == 0x63) d.ticks += wait63;
        else if (op >= 0x70 && op <= 0x7f) d.ticks += (op & 15)+1;
        else if (op == 0x64) {
            need(3); const auto target = quint8(data[pos++]);
            const int length = qFromLittleEndian<quint16>(data.constData()+pos); pos += 2;
            require(target == 0x62 || target == 0x63, "Invalid VGM wait override.");
            (target == 0x62 ? wait62 : wait63) = length;
        } else throw std::runtime_error(QString("Unsupported command 0x%1 in POKEY VGM.").arg(op,2,16,QLatin1Char('0')).toStdString());
    }
    require(endFound, "POKEY VGM has no end command.");
    require(!loopOffset || (loopFound && d.ticks > d.loopTick), "Invalid or zero-duration VGM loop.");
    if (loopOffset) d.loopDuration = d.ticks-d.loopTick;
    info_.path = path; info_.songs = 1; info_.song = 0;
    info_.title = QFileInfo(path).completeBaseName(); info_.system = "Atari 8-bit / 5200";
    applyVgmTags(data, info_.title, info_.system);
    const auto total = word(0x18);
    if (total) info_.durationMs = int(quint64(total)*1000/SampleRate);
    info_.loopStartMs = int(std::min<qint64>(d.loopTick*1000/SampleRate, std::numeric_limits<int>::max()));
    info_.voices = {"POKEY 1", "POKEY 2", "POKEY 3", "POKEY 4"};
    info_.configuration = vgmSoundConfiguration(data, info_.voices, h);
    if (playlist) info_.playlist.push_back({info_.title, info_.durationMs, info_.loopStartMs});
    d.reset();
}
PokeyTrack::~PokeyTrack() = default;
TrackBoundary PokeyTrack::boundary() const {
    return {impl_->ticks,impl_->loopDuration?impl_->loopTick:-1};
}
bool PokeyTrack::hasTaps() const { return impl_->capture; }
void PokeyTrack::render(short *out, int frames) {
    auto &d = *impl_;
    while (frames > 0) {
        if (d.first == d.available) d.fill();
        const int count = std::min(frames, d.available-d.first);
        for (int i = 0; i < count; ++i) {
            int mix = 0;
            for (int channel = 0; channel < 4; ++channel) {
                const auto sample = d.pending[channel*Batch+d.first+i];
                if (d.capture) d.history[channel][(frame_+i) % ScopeFrames] = sample;
                if (!(muteMask_ & (1u << channel))) mix += sample;
            }
            const auto sample = short(std::clamp(mix, -32768, 32767));
            *out++ = sample; *out++ = sample;
        }
        d.first += count; frame_ += count; frames -= count;
    }
}
bool PokeyTrack::seek(qint64 target, const std::function<bool()> &cancelled) {
    target = std::clamp<qint64>(target, 0, 3600LL*SampleRate);
    if (target < frame_) { impl_->reset(); frame_ = 0; }
    std::array<short, BlockFrames*2> scratch;
    while (frame_ < target) {
        if (cancelled()) return false;
        render(scratch.data(), int(std::min<qint64>(BlockFrames, target-frame_)));
    }
    return !cancelled();
}
bool PokeyTrack::readTap(int channel, QVector<float> &left, QVector<float> &right) const {
    if (!impl_->capture || channel < 0 || channel >= 4) return false;
    left.resize(ScopeFrames);
    for (int i = 0; i < ScopeFrames; ++i) {
        const auto sample = frame_-ScopeFrames+i;
        left[i] = sample < 0 ? 0 : impl_->history[channel][sample % ScopeFrames];
    }
    right = left; return true;
}

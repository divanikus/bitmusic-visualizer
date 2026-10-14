#include "yamahatrack.h"
#include "vgm.h"
#include "romfiles.h"
#include <ymfm_opn.h>
#include <ymfm_opm.h>
#include <ymfm_opl.h>
#include <QFileInfo>
#include <QStringConverter>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
using Stereo = std::array<double, 2>;
constexpr int Taps = 64, Phases = 128;
constexpr double Pi = 3.14159265358979323846;
constexpr quint32 MaxSampleMemory = 1u << 24; // ADPCM address bus, per bank.

struct SampleBlock {
    int bank; // 0 = ADPCM-A, 1 = ADPCM-B.
    quint32 total, offset;
    QByteArray bytes;
};

class Chip {
public:
    virtual ~Chip() = default;
    virtual void write(int reg, int value) = 0;
    virtual Stereo sample(qint64 frame) = 0;
    virtual void load(const SampleBlock &block) = 0;
};

template<class Core> class YamahaChip final : public Chip, public ymfm::ymfm_interface {
public:
    explicit YamahaChip(const VgmYamahaChip &spec, std::shared_ptr<const YamahaRoms> roms) : spec_(spec), roms_(std::move(roms)), core_(*this) {
        if constexpr (std::is_same_v<Core, ymfm::ym2203> || std::is_same_v<Core, ymfm::ym2608> ||
                      std::is_base_of_v<ymfm::ym2610, Core>) core_.set_fidelity(ymfm::OPN_FIDELITY_MIN);
        core_.reset();
        // Use an exact clock divider, not a rounded integer native sample rate.
        divider_ = int(72000000u / core_.sample_rate(72000000u));
        nextSample_ = divider_;
        const double cutoff = .46 * std::min(1.0, double(SampleRate)*divider_/spec_.clock);
        for (int phase = 0; phase < Phases; ++phase) {
            double sum = 0;
            for (int tap = 0; tap < Taps; ++tap) {
                const double x = tap + double(phase)/Phases - (Taps/2-1);
                const double sinc = std::abs(x)<1e-9 ? 2*cutoff : std::sin(2*Pi*cutoff*x)/(Pi*x);
                const double window = .42 + .5*std::cos(Pi*x/(Taps/2)) + .08*std::cos(2*Pi*x/(Taps/2));
                sum += filter_[phase][tap] = sinc*window;
            }
            for (auto &value : filter_[phase]) value /= sum;
        }
    }
    void write(int reg, int value) override {
        core_.write(2*(reg>>8), reg & 255);
        core_.write(2*(reg>>8)+1, value);
    }
    void load(const SampleBlock &block) override {
        auto &memory = memory_[block.bank];
        memory.resize(block.total, 0xff);
        std::copy(block.bytes.begin(), block.bytes.end(), memory.begin()+block.offset);
    }
    Stereo sample(qint64 frame) override {
        const qint64 target = (frame+1)*spec_.clock/SampleRate;
        for (;;) {
            const auto timer = std::min(timers_[0], timers_[1]);
            const auto next = std::min(nextSample_, timer);
            if (next > target) break;
            now_ = next;
            if (timer <= nextSample_) {
                for (int t = 0; t < 2; ++t) if (timers_[t] == now_) {
                    timers_[t] = Never;
                    m_engine->engine_timer_expired(t);
                }
            } else {
                typename Core::output_data output;
                core_.generate(&output);
                Stereo s;
                if constexpr (std::is_same_v<Core, ymfm::ym2203>) {
                    const double mix = output.data[0]*spec_.gain +
                        (output.data[1]+output.data[2]+output.data[3])*spec_.ssgGain;
                    s = {mix, mix};
                } else if constexpr (std::is_same_v<Core, ymfm::ym2608> || std::is_base_of_v<ymfm::ym2610, Core>) {
                    s = {output.data[0]*spec_.gain+output.data[2]*spec_.ssgGain,
                         output.data[1]*spec_.gain+output.data[2]*spec_.ssgGain};
                } else if constexpr (Core::OUTPUTS == 1) {
                    s = {output.data[0]*spec_.gain, output.data[0]*spec_.gain};
                } else if constexpr (std::is_same_v<Core, ymfm::ymf262>) {
                    // Fold OPL3's two stereo output pairs into the host L/R pair.
                    s = {(output.data[0]+output.data[2])*spec_.gain, (output.data[1]+output.data[3])*spec_.gain};
                } else s = {output.data[0]*spec_.gain, output.data[1]*spec_.gain};
                if (spec_.pan < 0) s[1] = 0;
                if (spec_.pan > 0) s[0] = 0;
                history_[cursor_] = s; cursor_ = (cursor_+1)%Taps;
                nextSample_ += divider_;
            }
        }
        now_ = target;
        const auto phase = int((target % divider_)*Phases/divider_);
        Stereo result{};
        for (int tap = 0; tap < Taps; ++tap) {
            const auto &s = history_[(cursor_+Taps-1-tap)%Taps];
            result[0] += s[0]*filter_[phase][tap]; result[1] += s[1]*filter_[phase][tap];
        }
        return result;
    }
private:
    uint8_t ymfm_external_read(ymfm::access_class type, uint32_t offset) override {
        if (type != ymfm::ACCESS_ADPCM_A && type != ymfm::ACCESS_ADPCM_B) return 0xff;
        if constexpr (std::is_same_v<Core, ymfm::ym2608>) {
            if (type == ymfm::ACCESS_ADPCM_A) {
                if (roms_->rhythm.isEmpty()) missingYamahaRom(*roms_);
                return offset < uint32_t(roms_->rhythm.size()) ? uint8_t(roms_->rhythm[offset]) : 0xff;
            }
        }
        const auto &memory = memory_[type == ymfm::ACCESS_ADPCM_A ? 0 : 1];
        if (memory.empty()) throw std::runtime_error((spec_.name + " sample memory is missing from this VGM.").toStdString());
        return offset < memory.size() ? memory[offset] : 0xff;
    }
    void ymfm_external_write(ymfm::access_class type, uint32_t offset, uint8_t value) override {
        if (type != ymfm::ACCESS_ADPCM_B) return; // Chip I/O pins have no host connection.
        require(offset < MaxSampleMemory, "Yamaha sample-memory address is out of range.");
        auto &memory = memory_[1];
        if (memory.size() <= offset) memory.resize(offset+1, 0xff);
        memory[offset] = value;
    }
    void ymfm_set_timer(uint32_t timer, int32_t duration) override {
        timers_[timer] = duration < 0 ? Never : now_ + std::max(1, duration);
    }
    static constexpr qint64 Never = std::numeric_limits<qint64>::max();
    VgmYamahaChip spec_;
    std::shared_ptr<const YamahaRoms> roms_;
    std::array<std::vector<uint8_t>,2> memory_;
    Core core_;
    qint64 now_ = 0, nextSample_ = 0;
    std::array<qint64,2> timers_{Never,Never};
    int divider_ = 0, cursor_ = 0;
    std::array<Stereo,Taps> history_{};
    std::array<std::array<double,Taps>,Phases> filter_{};
};
std::unique_ptr<Chip> makeChip(const VgmYamahaChip &spec, const std::shared_ptr<const YamahaRoms> &roms) {
    switch (spec.id) {
    case 3: return std::make_unique<YamahaChip<ymfm::ym2151>>(spec,roms);
    case 6: return std::make_unique<YamahaChip<ymfm::ym2203>>(spec,roms);
    case 7: return std::make_unique<YamahaChip<ymfm::ym2608>>(spec,roms);
    case 8:
        if (spec.variant) return std::make_unique<YamahaChip<ymfm::ym2610b>>(spec,roms);
        return std::make_unique<YamahaChip<ymfm::ym2610>>(spec,roms);
    case 9: return std::make_unique<YamahaChip<ymfm::ym3812>>(spec,roms);
    case 10: return std::make_unique<YamahaChip<ymfm::ym3526>>(spec,roms);
    case 11: return std::make_unique<YamahaChip<ymfm::y8950>>(spec,roms);
    case 12: return std::make_unique<YamahaChip<ymfm::ymf262>>(spec,roms);
    default: throw std::runtime_error("Unsupported Yamaha chip.");
    }
}
}

struct YamahaTrack::Impl {
    struct Event { qint64 tick; int chip, reg, value; };
    QVector<VgmYamahaChip> specs;
    QVector<Event> events;
    QVector<SampleBlock> blocks;
    std::shared_ptr<const YamahaRoms> roms;
    std::vector<std::unique_ptr<Chip>> chips;
    std::vector<std::array<std::array<float, ScopeFrames>,2>> history;
    qsizetype next = 0, loopEvent = 0;
    qint64 ticks = 0, loopTick = 0, loopDuration = 0, iteration = 0;
    bool capture = false;
    double gain = 1;
    void reset() {
        chips.clear();
        for (const auto &s : specs) chips.push_back(makeChip(s,roms));
        history.clear(); if (capture) history.resize(chips.size());
        next = 0; iteration = 0;
    }
    bool writes(qint64 frame) {
        while (true) {
            if (next == events.size()) {
                if (frame < iteration+ticks) return true;
                if (!loopDuration) return false;
                iteration += loopDuration; next = loopEvent;
            }
            if (next == events.size()) return true; // Loop containing only waits.
            const auto &e = events[next];
            if (e.tick+iteration > frame) return true;
            if (e.reg < 0) chips[e.chip]->load(blocks[e.value]);
            else chips[e.chip]->write(e.reg, e.value);
            ++next;
        }
    }
};

YamahaTrack::YamahaTrack(const QString &path, QByteArray data, int song, bool playlist, bool capture,
                       std::shared_ptr<const YamahaRoms> roms)
    : impl_(std::make_unique<Impl>()) {
    require(song == 0, "Invalid subsong number.");
    const auto h = inspectVgm(data); requireSupportedVgm(h);
    auto word = [&](qsizetype at) { return qFromLittleEndian<quint32>(data.constData()+at); };
    require(4ULL+word(4) == quint64(data.size()), "Truncated or inconsistent VGM file size.");
    auto &d = *impl_; d.specs = h.yamaha; d.capture = capture;
    require(!d.specs.isEmpty(), "No supported Yamaha chip in VGM.");
    if (std::any_of(d.specs.begin(),d.specs.end(),[](const auto &s){return s.id == 7;}))
        info_.yamahaRoms = d.roms = roms ? std::move(roms) : loadYamahaRoms();
    auto chipIndex = [&](int id, int instance) {
        const auto it = std::find_if(d.specs.begin(),d.specs.end(),[&](const auto &s){return s.id == id && s.instance == instance;});
        require(it != d.specs.end(), "VGM command targets an undeclared Yamaha chip.");
        return int(it-d.specs.begin());
    };
    const quint64 loop = word(0x1c) ? 0x1cULL+word(0x1c) : 0;
    bool loopFound = false, endFound = false;
    int wait62 = 735, wait63 = 882;
    std::array<int,2> loopWait{};
    std::array<bool,2> loopOverride{}, loopUsesEntryWait{};
    for (qsizetype pos = h.dataOffset; pos < data.size();) {
        if (quint64(pos) == loop) {
            d.loopTick = d.ticks; d.loopEvent = d.events.size(); loopFound = true;
            loopWait = {wait62,wait63};
        }
        const int op = quint8(data[pos++]);
        auto need = [&](int n) { require(data.size()-pos >= n, "Truncated Yamaha VGM command."); };
        if (op == 0x66) { endFound = true; break; }
        if (op == 0x61) { need(2); d.ticks += qFromLittleEndian<quint16>(data.constData()+pos); pos += 2; }
        else if (op == 0x62 || op == 0x63) {
            d.ticks += op == 0x62 ? wait62 : wait63;
            if (loopFound && !loopOverride[op-0x62]) loopUsesEntryWait[op-0x62] = true;
        }
        else if (op >= 0x70 && op <= 0x7f) d.ticks += (op&15)+1;
        else if (op == 0x64) {
            need(3); const auto target = quint8(data[pos++]);
            const int length = qFromLittleEndian<quint16>(data.constData()+pos); pos += 2;
            require(target == 0x62 || target == 0x63, "Invalid VGM wait override.");
            (target == 0x62 ? wait62 : wait63) = length;
            if (loopFound) loopOverride[target-0x62] = true;
        } else if (op == 0x67) {
            need(6); require(quint8(data[pos++]) == 0x66, "Invalid VGM data-block marker.");
            const auto type = quint8(data[pos++]);
            const auto rawSize = word(pos); pos += 4;
            const auto size = rawSize & 0x7fffffffu;
            require(size >= 8 && quint64(size) <= quint64(data.size()-pos), "Truncated VGM sample block.");
            const int id = type == 0x81 ? 7 : type == 0x82 || type == 0x83 ? 8 : type == 0x88 ? 11 : -1;
            require(id >= 0, "Unsupported Yamaha VGM data-block type.");
            const int chip = chipIndex(id,int(rawSize>>31));
            const auto total = word(pos), offset = word(pos+4);
            require(total && total <= MaxSampleMemory && offset <= total && size-8 <= total-offset,
                    "Invalid Yamaha VGM sample-memory range (maximum 16 MiB per bank).");
            require(d.blocks.size() < 65536 && d.events.size() < 2000000, "Yamaha VGM has too many sample blocks.");
            d.events.push_back({d.ticks,chip,-1,int(d.blocks.size())});
            d.blocks.push_back({type == 0x82 ? 0 : 1,total,offset,data.mid(pos+8,size-8)});
            pos += size;
        } else {
            const int primary = op >= 0xa0 && op <= 0xaf ? op-0x50 : op;
            const int id = primary == 0x54 ? 3 : primary == 0x55 ? 6 : primary == 0x56 || primary == 0x57 ? 7 :
                primary == 0x58 || primary == 0x59 ? 8 : primary == 0x5a ? 9 : primary == 0x5b ? 10 :
                primary == 0x5c ? 11 : primary == 0x5e || primary == 0x5f ? 12 : -1;
            if (id < 0) throw std::runtime_error(QString("Unsupported command 0x%1 in Yamaha VGM.").arg(op,2,16,QLatin1Char('0')).toStdString());
            need(2); const int reg = quint8(data[pos++]) + (primary == 0x57 || primary == 0x59 || primary == 0x5f ? 256 : 0), value = quint8(data[pos++]);
            require(d.events.size() < 2000000, "Yamaha VGM has too many register writes.");
            d.events.push_back({d.ticks,chipIndex(id,op >= 0xa0),reg,value});
        }
    }
    require(endFound, "Yamaha VGM has no end command.");
    require(!loop || (loopFound && d.ticks > d.loopTick), "Invalid or zero-duration VGM loop.");
    require(!(loopUsesEntryWait[0] && loopWait[0] != wait62) && !(loopUsesEntryWait[1] && loopWait[1] != wait63),
            "Unsupported VGM loop with changing wait lengths.");
    if (loop) d.loopDuration = d.ticks-d.loopTick;
    const auto total = word(0x18);
    require(!total || total == d.ticks, "VGM duration disagrees with its command stream.");
    info_.path = path; info_.songs = 1; info_.song = 0;
    info_.title = QFileInfo(path).completeBaseName(); info_.system = "Yamaha VGM";
    const quint64 gd3 = word(0x14) ? 0x14ULL+word(0x14) : 0;
    if (gd3 && gd3+12 <= quint64(data.size()) && data.mid(qsizetype(gd3),4) == "Gd3 ") {
        const auto size = word(qsizetype(gd3+8));
        if (!(size&1) && size <= 1024*1024 && gd3+12+size <= quint64(data.size())) {
            QStringDecoder decode(QStringDecoder::Utf16LE);
            const QString text = decode(QByteArrayView(data).sliced(qsizetype(gd3+12),size));
            const auto fields = text.split(QChar(0));
            if (!fields.value(0).trimmed().isEmpty()) info_.title = fields[0].trimmed();
            if (!fields.value(4).trimmed().isEmpty()) info_.system = fields[4].trimmed();
        }
    }
    if (total) info_.durationMs = int(quint64(total)*1000/SampleRate);
    info_.loopStartMs = int(std::min<qint64>(d.loopTick*1000/SampleRate,std::numeric_limits<int>::max()));
    double weight = 0;
    for (const auto &s : d.specs) {
        const bool dual = std::count_if(d.specs.begin(),d.specs.end(),[&](const auto &v){return v.id==s.id;}) > 1;
        info_.voices << s.name + (dual ? QString(" %1").arg(s.instance+1) : QString()) + " mix";
        info_.stereoOutput |= s.stereo;
        weight += std::max(s.gain,s.ssgGain);
    }
    d.gain = .5/std::max(1.0,weight); // Fixed headroom; never changes when muting a chip.
    info_.configuration = soundConfiguration(data,info_.voices);
    if (playlist) info_.playlist.push_back({info_.title,info_.durationMs,info_.loopStartMs});
    d.reset();
}
YamahaTrack::~YamahaTrack() = default;
bool YamahaTrack::hasTaps() const { return impl_->capture; }
void YamahaTrack::render(short *out, int frames) {
    auto &d = *impl_;
    for (int i = 0; i < frames; ++i, ++frame_) {
        const bool running = d.writes(frame_);
        int mix[2]{};
        for (size_t c = 0; c < d.chips.size(); ++c) {
            const auto s = running ? d.chips[c]->sample(frame_) : Stereo{};
            for (int side = 0; side < 2; ++side) {
                const int value = int(std::clamp(std::lround(s[side]*d.gain),-32768L,32767L));
                if (d.capture) d.history[c][side][frame_%ScopeFrames] = float(value);
                if (!(muteMask_ & (1u<<c))) mix[side] += value;
            }
        }
        *out++ = short(std::clamp(mix[0],-32768,32767)); *out++ = short(std::clamp(mix[1],-32768,32767));
    }
}
bool YamahaTrack::seek(qint64 target, const std::function<bool()> &cancelled) {
    target = std::clamp<qint64>(target,0,3600LL*SampleRate);
    if (target < frame_) { impl_->reset(); frame_ = 0; }
    std::array<short,BlockFrames*2> scratch;
    while (frame_ < target) {
        if (cancelled()) return false;
        render(scratch.data(),int(std::min<qint64>(BlockFrames,target-frame_)));
    }
    return !cancelled();
}
bool YamahaTrack::readTap(int channel, QVector<float> &left, QVector<float> &right) const {
    if (!impl_->capture || channel < 0 || size_t(channel) >= impl_->chips.size()) return false;
    left.resize(ScopeFrames); right.resize(ScopeFrames);
    for (int i = 0; i < ScopeFrames; ++i) {
        const auto at = frame_-ScopeFrames+i;
        left[i] = at < 0 ? 0 : impl_->history[channel][0][at%ScopeFrames];
        right[i] = at < 0 ? 0 : impl_->history[channel][1][at%ScopeFrames];
    }
    return true;
}

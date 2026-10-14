#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <QDataStream>
#include <QDebug>
#include <QTextStream>

// Bounded by all currently supported dual Yamaha combinations, with room for
// expansion. Full mix is a separate source ID, never a bit in this mask.
struct VoiceMask {
    static constexpr int Capacity = 256;
    std::array<uint64_t,4> words{};
    VoiceMask(uint64_t low = 0) { words[0] = low; }
    static VoiceMask bit(int index) {
        VoiceMask v;
        if (index >= 0 && index < Capacity) v.words[index/64] = uint64_t(1) << (index%64);
        return v;
    }
    static VoiceMask first(int count) {
        VoiceMask v; for (int i=0;i<count && i<Capacity;++i) v |= bit(i); return v;
    }
    bool test(int index) const {
        return index >= 0 && index < Capacity && (words[index/64] & (uint64_t(1) << (index%64)));
    }
    uint32_t slice(int offset, int count) const {
        uint32_t value=0;
        for(int i=0;i<count && i<32;++i) if(test(offset+i)) value |= 1u<<i;
        return value;
    }
    uint32_t low32() const { return uint32_t(words[0]); }
    explicit operator bool() const { for(auto w:words) if(w) return true; return false; }
    VoiceMask operator~() const { auto v=*this; for(auto &w:v.words) w=~w; return v; }
    VoiceMask &operator|=(VoiceMask v) { for(int i=0;i<4;++i) words[i]|=v.words[i]; return *this; }
    VoiceMask &operator&=(VoiceMask v) { for(int i=0;i<4;++i) words[i]&=v.words[i]; return *this; }
    VoiceMask &operator^=(VoiceMask v) { for(int i=0;i<4;++i) words[i]^=v.words[i]; return *this; }
    friend VoiceMask operator|(VoiceMask a,VoiceMask b) { return a|=b; }
    friend VoiceMask operator&(VoiceMask a,VoiceMask b) { return a&=b; }
    friend VoiceMask operator^(VoiceMask a,VoiceMask b) { return a^=b; }
    friend bool operator==(VoiceMask a,VoiceMask b) { return a.words==b.words; }
    friend bool operator!=(VoiceMask a,VoiceMask b) { return !(a==b); }
    friend QDataStream &operator<<(QDataStream &out,VoiceMask v) { for(auto w:v.words) out<<quint64(w); return out; }
    friend QDebug operator<<(QDebug out,VoiceMask v) { QDebugStateSaver save(out); out.nospace()<<"VoiceMask("; for(auto w:v.words) out<<Qt::hex<<w<<' '; out<<')'; return out; }
    friend QTextStream &operator<<(QTextStream &out,VoiceMask v) { for(int i=3;i>=0;--i) out<<QString::number(v.words[i],16).rightJustified(16,'0'); return out; }
};

// These controls are read by decoder/UI workers, never by the audio callback.
// A lock keeps a multiword change coherent on every supported architecture.
class SharedVoiceMask {
public:
    SharedVoiceMask(VoiceMask v={}) : value_(v) {}
    VoiceMask load() const { std::lock_guard lock(mutex_); return value_; }
    void store(VoiceMask v) { std::lock_guard lock(mutex_); value_=v; }
    VoiceMask exchange(VoiceMask v) { std::lock_guard lock(mutex_); auto old=value_; value_=v; return old; }
    void fetch_or(VoiceMask v) { std::lock_guard lock(mutex_); value_|=v; }
    explicit operator bool() const { return bool(load()); }
private:
    mutable std::mutex mutex_;
    VoiceMask value_;
};

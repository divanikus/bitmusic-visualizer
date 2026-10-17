#include "midisequence.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool ok, const char *s) { if (!ok) throw std::runtime_error(s); }
struct Reader {
    const QByteArray &data; qsizetype pos = 0, end;
    void need(qint64 n) const { require(n >= 0 && n <= end-pos, "Truncated MIDI file."); }
    int byte() { need(1); return quint8(data[pos++]); }
    quint32 word(int n) { quint32 v = 0; while (n--) v = (v << 8) | byte(); return v; }
    quint32 vlq() {
        quint32 value = 0;
        for (int i = 0; i < 4; ++i) { int b = byte(); value = (value << 7) | (b & 127); if (!(b & 128)) return value; }
        throw std::runtime_error("Invalid MIDI variable-length value.");
    }
    QByteArray take(qint64 n) { need(n); auto b = data.mid(pos,n); pos += n; return b; }
};
constexpr qsizetype MaxEvents = 2000000;
struct TickEvent { quint64 tick; int tempo; QByteArray message; int port = -1; };
void usePort(int &output, int port) {
    if (port < 0) return; // Unaddressed conductor SysEx follows the selected output.
    require(output < 0 || output == port, "Multiple MIDI ports are not supported yet.");
    output = port;
}
// Note-free SMF 2 tracks supply initial setup to subsequent musical patterns.
// Preserve message order (bank/program, RPN/NRPN and SysEx can be stateful), but
// use their final tempo and omit their silent duration and note releases.
struct PatternSetup {
    QVector<TickEvent> messages;
    int tempo = 0;
    bool mt32 = false;
    void append(const QVector<TickEvent> &events, bool hasMt32) {
        for (const auto &event : events) {
            if (event.tempo) { tempo = event.tempo; continue; }
            const int kind = quint8(event.message[0]) & 0xf0;
            if (kind != 0x80 && kind != 0x90)
                messages.push_back({0, 0, event.message, event.port});
        }
        mt32 |= hasMt32;
    }
    QVector<TickEvent> prepend(QVector<TickEvent> events, int outputPort) const {
        if (messages.isEmpty() && !tempo) return events;
        for (const auto &event : messages) usePort(outputPort, event.port);
        QVector<TickEvent> combined;
        combined.reserve(messages.size() + events.size() + bool(tempo));
        if (tempo) combined.push_back({0, tempo, {}});
        combined += messages;
        combined += events; // Pattern events at tick zero override initial setup.
        return combined;
    }
};
MidiSequence finishSequence(QVector<TickEvent> events, MidiSequence result, quint64 endTick, quint32 division, int rate) {
    std::stable_sort(events.begin(),events.end(),[](const auto &a,const auto &b){ return a.tick<b.tick; });
    double secondsPerTick = 0;
    if (division & 0x8000) {
        const int fps=-int(qint8(division>>8)), ticks=division&255;
        require(ticks && (fps==24 || fps==25 || fps==29 || fps==30),"Invalid MIDI SMPTE division.");
        secondsPerTick=1.0/((fps==29 ? 30000.0/1001.0 : fps)*ticks);
    }
    long double seconds=0; quint64 last=0; int tempo=500000;
    const auto advance=[&](quint64 tick) {
        seconds+=(tick-last)*(secondsPerTick ? secondsPerTick : tempo/(1000000.0L*division)); last=tick;
        require(seconds <= 3600,"MIDI duration exceeds one hour.");
        return qint64(std::llround(seconds*rate));
    };
    for (const auto &event : events) {
        const auto frame=advance(event.tick);
        if (event.tempo) tempo=event.tempo;
        else result.events.push_back({frame,event.message});
    }
    result.endFrame=advance(endTick);
    return result;
}
}
QVector<MidiSequence> MidiSequence::parseSongs(const QByteArray &bytes, int rate) {
    require(rate > 0 && bytes.size() <= 64*1024*1024, "MIDI exceeds the 64 MiB limit.");
    Reader r{bytes,0,bytes.size()};
    require(r.take(4) == "MThd", "Not a Standard MIDI file.");
    const auto header = r.word(4);
    require(header >= 6 && header <= quint64(r.end-r.pos), "Invalid MIDI header.");
    const auto format = r.word(2), tracks = r.word(2), division = r.word(2);
    require(format <= 2, "Unsupported MIDI format.");
    require(tracks && tracks <= 1024 && (format != 0 || tracks == 1), "Invalid MIDI track count.");
    require(division != 0, "Invalid MIDI time division.");
    r.take(header-6);
    QVector<TickEvent> events;
    QVector<MidiSequence> songs;
    qsizetype eventCount = 0;
    qsizetype preparedEvents = 0;
    PatternSetup setup;
    MidiSequence result;
    quint64 endTick = 0;
    int outputPort=-1;
    for (quint32 track = 0; track < tracks; ++track) {
        require(r.take(4) == "MTrk", "Expected MIDI track chunk.");
        const auto size = r.word(4); r.need(size);
        Reader t{bytes,r.pos,r.pos+size}; r.pos += size;
        quint64 tick = 0; int running = 0, port = -1, sysexPort = -1; bool ended = false; QByteArray sysex;
        while (t.pos < t.end) {
            tick += t.vlq();
            require(++eventCount <= MaxEvents, "MIDI has too many events.");
            int status = t.byte();
            if (status < 128) { require(running != 0, "Invalid MIDI running status."); --t.pos; status = running; }
            if (status < 0xf0) {
                require(sysex.isEmpty(), "MIDI channel event inside unfinished SysEx.");
                running = status;
                QByteArray msg(1,char(status));
                const int count = (status & 0xe0) == 0xc0 ? 1 : 2;
                for (int i=0;i<count;++i) { int b=t.byte(); require(b<128,"Invalid MIDI event data."); msg += char(b); }
                if ((status & 0xf0) == 0x90 && quint8(msg[2])) result.noteChannels |= quint16(1u << (status & 15));
                usePort(outputPort,port<0?0:port); events.push_back({tick,0,msg,port<0?0:port});
            } else if (status == 0xff) {
                running = 0;
                const int type=t.byte(); const auto data=t.take(t.vlq());
                if (type == 0x2f) {
                    require(data.isEmpty() && sysex.isEmpty() && t.pos==t.end, "Invalid MIDI end-of-track.");
                    ended = true; break;
                }
                if (type == 0x51) {
                    require(data.size()==3,"Invalid MIDI tempo.");
                    const int tempo=(quint8(data[0])<<16)|(quint8(data[1])<<8)|quint8(data[2]);
                    require(tempo>0,"Invalid MIDI tempo."); events.push_back({tick,tempo,{}});
                } else if (type==3 && result.title.isEmpty()) result.title=QString::fromUtf8(data).trimmed().left(512);
                else if (type==0x21) {
                    require(data.size()==1,"Invalid MIDI port prefix."); port=quint8(data[0]);
                }
            } else if (status == 0xf0 || status == 0xf7) {
                running = 0;
                if (status==0xf0) { require(sysex.isEmpty(),"Nested MIDI SysEx."); sysex += char(0xf0); sysexPort=port; }
                require(!sysex.isEmpty(),"Standalone MIDI escape events are not supported yet.");
                sysex += t.take(t.vlq());
                require(sysex.size()<=1024*1024,"MIDI SysEx exceeds 1 MiB.");
                if (sysex.endsWith(char(0xf7))) {
                    for (qsizetype i=1;i<sysex.size()-1;++i) require(quint8(sysex[i])<128,"Invalid MIDI SysEx data.");
                    result.mt32 |= sysex.size()>=5 && quint8(sysex[1])==0x41 && quint8(sysex[3])==0x16;
                    usePort(outputPort,sysexPort); events.push_back({tick,0,sysex,sysexPort}); sysex.clear();
                }
            } else throw std::runtime_error("Unsupported system event in MIDI file.");
        }
        require(ended,"MIDI track has no end-of-track event."); endTick=std::max(endTick,tick);
        if (format == 2) {
            if (result.noteChannels) {
                preparedEvents += events.size() + setup.messages.size() + bool(setup.tempo);
                require(preparedEvents <= MaxEvents, "MIDI setup expansion has too many events.");
                result.mt32 |= setup.mt32;
                songs.push_back(finishSequence(setup.prepend(std::move(events), outputPort),
                    std::move(result), endTick, division, rate));
            } else {
                setup.append(events, result.mt32);
                // Still validate timing and every byte in hidden/trailing setup.
                finishSequence(std::move(events), std::move(result), endTick, division, rate);
            }
            events.clear(); result = {}; endTick = 0; outputPort = -1;
        }
    }
    require(r.pos==r.end,"Unexpected data after MIDI tracks.");
    if (format != 2) songs.push_back(finishSequence(std::move(events), std::move(result), endTick, division, rate));
    require(!songs.isEmpty(), "MIDI format 2 file contains no notes.");
    return songs;
}
MidiSequence MidiSequence::parse(const QByteArray &bytes, int rate, int song) {
    auto songs = parseSongs(bytes, rate);
    require(song >= 0 && song < songs.size(), "MIDI subsong index is out of range.");
    return std::move(songs[song]);
}

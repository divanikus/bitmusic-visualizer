// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from TIATracker 1.3 track/* and emulation/player.cpp.
// Copyright 2016-2017 Andre "Kylearan" Wichmann.
// Copyright 2026 Bit Music Visualizer contributors.
// Modified 2026-10-17: bounded validated loading, offline register sequencing,
// complete replay-state loop detection; no editor, SDL, timers or audio devices.
#include "replay.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <stdexcept>

namespace Ttt {
namespace {
void require(bool ok, const QString &message) {
    if(!ok) throw std::runtime_error(("Invalid TIATracker project: "+message).toStdString());
}
int integer(const QJsonObject &o, const char *key, int low, int high) {
    const auto v=o[key]; const double n=v.toDouble();
    require(v.isDouble() && n>=low && n<=high && n==int(n), QString::fromLatin1(key));
    return int(n);
}
QJsonArray array(const QJsonObject &o,const char *key,int low,int high) {
    const auto v=o[key]; const auto a=v.toArray();
    require(v.isArray() && a.size()>=low && a.size()<=high,QString::fromLatin1(key)); return a;
}
QVector<int> numbers(const QJsonObject &o,const char *key,int count,int low,int high) {
    QVector<int> values;
    for(const auto &v:array(o,key,count,count)) {
        double n=v.toDouble(); require(v.isDouble() && n>=low && n<=high && n==int(n),QString::fromLatin1(key));
        values.push_back(int(n));
    }
    return values;
}
// Canonical hardware values used by TIATracker's distortion table.
constexpr int distortion[]={0,1,2,3,4,4,6,7,8,7,6,0,12,12,14,15,16};
struct Instrument { QVector<int> frequency,volume,wave; int sustain=0,release=0; bool overlay=false; };
struct Note { int type=0,number=-1,value=0; };
struct Pattern { QVector<Note> notes; int even=3,odd=3; };
struct Entry { int pattern,go; };
struct Channel {
    int entry=0,row=0,envelope=0,mode=0;
    Note note;
    bool overlay=false;
};
}
bool recognizes(const QByteArray &data) {
    // Signature is in the JSON structure, not its extension or title.
    const auto prefix=data.left(4096).trimmed();
    if(!prefix.startsWith('{') && !prefix.startsWith(QByteArray::fromHex("efbbbf"))) return false;
    const auto o=QJsonDocument::fromJson(data).object();
    return o.contains("tvmode") && o.contains("channels") && o.contains("patterns") && o.contains("instruments");
}
Song compile(const QByteArray &data) {
    require(data.size()<=16*1024*1024,"project exceeds 16 MB");
    QJsonParseError error;
    const auto doc=QJsonDocument::fromJson(data,&error);
    require(error.error==QJsonParseError::NoError && doc.isObject(),"JSON");
    const auto root=doc.object();
    integer(root,"version",1,1);
    Song song; song.title=root["metaName"].toString().trimmed();
    const auto tv=root["tvmode"].toString(); require(tv=="pal" || tv=="ntsc","TV mode");
    song.rate=tv=="pal"?50:60;
    const int even=integer(root,"evenspeed",1,255),odd=integer(root,"oddspeed",1,255);
    require(!root.contains("globalspeed") || root["globalspeed"].isBool(),"global speed");
    const bool global=root["globalspeed"].toBool(true);
    QVector<Instrument> instruments;
    for(int type=0;type<2;++type) {
        for(const auto &value:array(root,type?"percussion":"instruments",type?15:7,type?15:7)) {
            const auto o=value.toObject(); integer(o,"version",1,1);
            const int size=integer(o,"envelopeLength",type?1:2,99);
            Instrument ins;
            ins.frequency=numbers(o,"frequencies",size,type?0:-8,type?31:7);
            ins.volume=numbers(o,"volumes",size,0,15);
            if(type) {
                ins.wave=numbers(o,"waveforms",size,0,15);
                require(o["overlay"].isBool(),"percussion overlay"); ins.overlay=o["overlay"].toBool();
            } else {
                ins.wave={integer(o,"waveform",0,16)};
                ins.sustain=integer(o,"sustainStart",0,size-2);
                ins.release=integer(o,"releaseStart",ins.sustain+1,size-1);
            }
            for(auto &w:ins.wave) w=distortion[w];
            song.instruments<<o["name"].toString(); instruments<<ins;
        }
    }
    QVector<Pattern> patterns;
    int totalNotes=0;
    for(const auto &value:array(root,"patterns",1,4096)) {
        const auto o=value.toObject(); Pattern p;
        if(o.contains("evenspeed")) { p.even=integer(o,"evenspeed",1,255); p.odd=integer(o,"oddspeed",1,255); }
        for(const auto &v:array(o,"notes",1,65536)) {
            const auto n=v.toObject(); Note note;
            note.type=integer(n,"type",0,4);
            note.number=integer(n,"number",0,note.type==1?6:note.type==3?14:22);
            note.value=integer(n,"value",note.type==1?0:-255,note.type==1?63:255);
            if(note.type==3) note.number+=7;
            p.notes<<note;
        }
        totalNotes+=p.notes.size(); require(totalNotes<=262144,"too many pattern rows");
        patterns<<p;
    }
    std::array<QVector<Entry>,2> sequences;
    std::array<Channel,2> channels;
    const auto chans=array(root,"channels",2,2);
    for(int c=0;c<2;++c) {
        const auto seq=array(chans[c].toObject(),"sequence",1,4096);
        for(const auto &v:seq) {
            const auto e=v.toObject(); sequences[c].push_back({integer(e,"patternindex",0,patterns.size()-1),integer(e,"gototarget",-1,seq.size()-1)});
        }
        const char *key=c?"startpattern1":"startpattern0";
        if(root.contains(key)) channels[c].entry=integer(root,key,0,seq.size()-1);
    }
    auto pattern=[&](int c)->const Pattern& { return patterns[sequences[c][channels[c].entry].pattern]; };
    auto advance=[&](int c) {
        auto &s=channels[c];
        if(++s.row==pattern(c).notes.size()) {
            const auto e=sequences[c][s.entry]; s.entry=e.go<0?s.entry+1:e.go; s.row=0;
            if(s.entry>=sequences[c].size()) return false;
        }
        return true;
    };
    int countdown=0; bool first=true,ended=false; Tick tick;
    QHash<QByteArray,int> visited;
    // At most two hours of control data; never render PCM to discover duration.
    for(int time=0;time<song.rate*7200;++time) {
        QByteArray key;
        auto add=[&](int v){for(int b=0;b<4;++b) key.append(char(quint32(v)>>(8*b)));};
        add(countdown); add(first);
        for(int c=0;c<2;++c) {
            const auto &s=channels[c]; add(s.entry);add(s.row);add(s.envelope);add(s.mode);add(s.overlay);
            add(s.note.type);add(s.note.number);add(s.note.value);
            for(int v:tick.registers[c]) add(v);
            add(tick.instruments[c]);
        }
        const auto seen=visited.constFind(key);
        if(seen!=visited.cend()) {song.loop=seen.value();return song;}
        visited.insert(key,time);
        if(--countdown<0) {
            for(int c=0;c<2;++c) {
                auto &s=channels[c];
                if(!first && !s.overlay && !advance(c)) { ended=true;break; }
                const auto n=pattern(c).notes[s.row];
                switch(n.type) {
                case 0: break; // Hold leaves the current envelope running.
                case 1: if(!s.overlay) {s.mode=1;s.note=n;s.envelope=0;} break;
                case 2:
                    require(s.mode==1,"pause without a melodic instrument");
                    s.mode=0;s.envelope=instruments[s.note.number].release;break;
                case 3: s.mode=3;s.note=n;s.envelope=0;break;
                case 4:
                    require(s.mode==1,"slide without a melodic instrument");
                    s.note.value+=n.value;
                    require(s.note.value>=0 && s.note.value<=63,"slide frequency out of range");break;
                }
                s.overlay=false;
            }
            if(ended) return song;
            first=false;
            const auto &p=pattern(0);
            // TIATracker uses oddSpeed on its zero-based even rows.
            countdown=(channels[0].row%2 ? (global?even:p.even) : (global?odd:p.odd))-1;
        }
        for(int c=0;c<2;++c) {
            auto &s=channels[c];
            if(s.note.number<0) continue;
            const auto &ins=instruments[s.note.number];
            tick.instruments[c]=s.note.number;
            if(s.note.type==1) {
                const int control=ins.wave[0]==16?(s.note.value<32?4:12):ins.wave[0];
                if(s.envelope>=ins.volume.size()) { tick.registers[c]={control,0,0};continue; }
                tick.registers[c]={control,((s.note.value%32)+ins.frequency[s.envelope])&31,ins.volume[s.envelope]};
                if(++s.envelope==ins.release) s.envelope=ins.sustain;
            } else if(s.note.type==3) {
                if(!s.overlay && s.envelope<ins.volume.size()) {
                    tick.registers[c]={ins.wave[s.envelope],ins.frequency[s.envelope],ins.volume[s.envelope]};
                    if(++s.envelope==ins.volume.size() && ins.overlay) {
                        if(!advance(c)) {ended=true;break;}
                        s.overlay=true;
                        const auto next=pattern(c).notes[s.row];
                        if(next.type==1) {s.mode=1;s.note=next;s.envelope=instruments[next.number].sustain;}
                    }
                } else tick.registers[c]={ins.wave.last(),0,0};
            }
        }
        song.ticks<<tick;
        if(ended) return song;
    }
    throw std::runtime_error("TIATracker sequence exceeds the two-hour analysis limit.");
}
}

#include "mt32track.h"
#include "miditrack.h"
#include "romfiles.h"
#include "appsettings.h"
#include "resourceerror.h"
#include <mt32emu/mt32emu.h>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <deque>
#include <stdexcept>

namespace {
constexpr int Rate=32000, Chunk=64, Taps=64, Phases=128, TailSeconds=2;
constexpr double Pi=3.14159265358979323846;
using Sample=std::array<float,20>; // Master L/R, then nine dry stereo parts.
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
struct Rom {
    MT32Emu::ArrayFile file;
    const MT32Emu::ROMImage *image;
    explicit Rom(const QByteArray &bytes):file(reinterpret_cast<const MT32Emu::Bit8u *>(bytes.constData()),size_t(bytes.size())),image(MT32Emu::ROMImage::makeROMImage(&file)) {}
    ~Rom() { if(image) MT32Emu::ROMImage::freeROMImage(image); }
    Rom(const Rom&)=delete;
};
std::shared_ptr<const Mt32Resources> loadResources(const QByteArray &data) {
    auto r=std::make_shared<Mt32Resources>(); r->sequence=MidiSequence::parse(data,SampleRate);
    const QDir folder(playerMt32Directory());
    const auto read=[&](const QString &name) {
        QFile f(folder.filePath(name));
        if(!f.open(QIODevice::ReadOnly))
            throw std::runtime_error(resourceRequiredMessage(RequiredResource::Mt32Rom,
                "place MT32_CONTROL.ROM and MT32_PCM.ROM in:\n"+QDir::toNativeSeparators(folder.path())).toStdString());
        require(f.size()>0 && f.size()<=2*1024*1024,"Invalid MT-32 ROM size.");
        const auto bytes=f.readAll(); require(f.error()==QFile::NoError && bytes.size()==f.size(),"Cannot read MT-32 ROM.");
        return bytes;
    };
    r->control=read("MT32_CONTROL.ROM"); r->pcm=read("MT32_PCM.ROM");
    Rom control(r->control),pcm(r->pcm);
    const auto *c=control.image?control.image->getROMInfo():nullptr;
    const auto *p=pcm.image?pcm.image->getROMInfo():nullptr;
    require(c && p && c->type==MT32Emu::ROMInfo::Control && p->type==MT32Emu::ROMInfo::PCM &&
        c->pairType==MT32Emu::ROMInfo::Full && p->pairType==MT32Emu::ROMInfo::Full,
        "MT-32 ROMs are not recognized full Control/PCM images. Check their contents, not just their filenames.");
    bool compatible=false;
    for(auto machine=MT32Emu::MachineConfiguration::getAllMachineConfigurations();*machine;++machine) {
        bool hasControl=false,hasPcm=false;
        for(auto info=(*machine)->getCompatibleROMInfos();*info;++info) { hasControl|=*info==c; hasPcm|=*info==p; }
        compatible|=hasControl&&hasPcm;
    }
    require(compatible,"Incompatible MT-32 Control and PCM ROMs. Use a matching MT-32 or CM-32L pair.");
    r->model=QString::fromLatin1(c->shortName).contains("cm32",Qt::CaseInsensitive)?"Roland CM-32L":"Roland MT-32";
    // Release at the file's real end, preserving its entire timeline and rests.
    for(int ch=0;ch<16;++ch) for(int cc:{64,123}) {
        QByteArray m; m.append(char(0xb0|ch)); m.append(char(cc)); m.append(char(0));
        r->sequence.events.push_back({r->sequence.endFrame,m});
    }
    return r;
}
MT32Emu::Bit32u shortMessage(const QByteArray &m) {
    MT32Emu::Bit32u result=0;
    for(int i=0;i<std::min<qsizetype>(3,m.size());++i) result|=MT32Emu::Bit32u(quint8(m[i]))<<(8*i);
    return result;
}
}
QString playerMt32Directory() { return QDir(playerRomsDirectory()).filePath("mt32"); }
QString selectedMidiEngine() {
    QSettings s(playerSettingsPath(),playerSettingsFormat()); s.setFallbacksEnabled(false);
    const auto v=s.value("MIDI/Engine","auto").toString(); return v=="gm"||v=="mt32"?v:"auto";
}
void selectMidiEngine(const QString &engine) {
    require(engine=="auto"||engine=="gm"||engine=="mt32","Unknown MIDI engine.");
    QSettings s(playerSettingsPath(),playerSettingsFormat()); s.setFallbacksEnabled(false);
    s.setValue("MIDI/Engine",engine); s.sync(); require(s.status()==QSettings::NoError,"Cannot save MIDI engine.");
}
std::unique_ptr<MusicTrack> openMidiTrack(const QString &path,QByteArray data,int song,bool playlist,bool capture) {
    const auto choice=selectedMidiEngine();
    const bool mt32=choice=="mt32" || (choice=="auto" && MidiSequence::parse(data,SampleRate).mt32);
    if(mt32) return std::make_unique<Mt32Track>(path,std::move(data),song,playlist,capture);
    return std::make_unique<MidiTrack>(path,std::move(data),song,playlist,capture);
}

struct Mt32Track::Impl : MT32Emu::ReportHandler {
    std::shared_ptr<const Mt32Resources> resources;
    std::unique_ptr<Rom> control,pcm;
    std::unique_ptr<MT32Emu::Synth> synth; // Destroy before ROMs and their byte snapshot.
    std::array<Sample,Chunk> input{};
    std::array<Sample,Taps> filterHistory{};
    std::array<std::array<float,Taps>,Phases> filter{};
    std::vector<std::array<std::array<float,ScopeFrames>,2>> history;
    int inputAt=Chunk,cursor=0;
    qint64 generated=0,consumed=0;
    qsizetype event=0;
    bool capture=false,resetReported=false;
    MT32Emu::Bit32u captureStart=0;
    struct Names { qint64 frame; QStringList parts; };
    std::deque<Names> names;
    void printDebug(const char *,va_list) override {}
    void showLCDMessage(const char *) override {}
    void onDeviceReset() override { resetReported=true; }
    void rememberNames(qint64 frame) {
        QStringList parts;
        for(int i=0;i<8;++i) parts<<QString::fromLatin1(synth->getPatchName(i)).trimmed();
        parts<<"Rhythm";
        if(names.empty()||names.back().parts!=parts) names.push_back({frame,parts});
        while(names.size()>1 && names[1].frame<frame-SampleRate) names.pop_front();
    }
    static void partSample(void *ctx,unsigned part,MT32Emu::Bit32u timestamp,float left,float right) {
        auto &d=*static_cast<Impl *>(ctx); const auto at=timestamp-d.captureStart;
        if(part<9 && at<Chunk) { d.input[at][2+2*part]+=left*2; d.input[at][3+2*part]+=right*2; }
    }
    void reset() {
        synth.reset(); pcm.reset(); control.reset();
        control=std::make_unique<Rom>(resources->control); pcm=std::make_unique<Rom>(resources->pcm);
        require(control->image && pcm->image,"Cannot initialize MT-32 ROM images.");
        synth=std::make_unique<MT32Emu::Synth>(this);
        synth->selectRendererType(MT32Emu::RendererType_FLOAT);
        require(synth->open(*control->image,*pcm->image,32,MT32Emu::AnalogOutputMode_COARSE),"MT-32 synth cannot open this Control/PCM ROM pair.");
        synth->setMIDIDelayMode(MT32Emu::MIDIDelayMode_IMMEDIATE);
        synth->setMIDIEventQueueSize(4096);
        if(capture) { synth->bitmusicPartSample=partSample; synth->bitmusicPartContext=this; }
        input={}; filterHistory={}; inputAt=Chunk; cursor=0; generated=consumed=0; event=0;
        names.clear(); rememberNames(0); resetReported=false;
        if(capture) history.assign(9,{});
        for(int phase=0;phase<Phases;++phase) {
            double sum=0;
            for(int tap=0;tap<Taps;++tap) {
                const double x=tap+double(phase)/Phases-(Taps/2-1);
                const double sinc=std::abs(x)<1e-9?.92:std::sin(.92*Pi*x)/(Pi*x);
                const double window=.42+.5*std::cos(Pi*x/(Taps/2))+.08*std::cos(2*Pi*x/(Taps/2));
                sum+=filter[phase][tap]=float(sinc*window);
            }
            for(auto &v:filter[phase]) v=float(v/sum);
        }
    }
    bool enqueue(const QByteArray &m) {
        const auto now=synth->getInternalRenderedSampleCount();
        if(quint8(m[0])==0xf0) return synth->playSysex(reinterpret_cast<const MT32Emu::Bit8u *>(m.constData()),MT32Emu::Bit32u(m.size()),now);
        return synth->playMsg(shortMessage(m),now);
    }
    VoiceMask emptyParts(std::shared_ptr<const NoteRoll> &roll) {
        VoiceMask used;
        std::array<VoiceMask,16> routes{};
        const auto readRoutes=[&] {
            routes={}; MT32Emu::Bit8u assignment[9]{};
            synth->readMemory((0x10u<<14)+13,9,assignment);
            for(int part=0;part<9;++part) if(assignment[part]<16)
                routes[assignment[part]] |= VoiceMask::bit(part);
        };
        readRoutes(); bool changed=false;
        // Let Munt validate SysEx and resolve routing, including resets and
        // remapping later in the song. No notes or audio are synthesized here.
        roll=midiNoteRoll(resources->sequence,[&](const MidiEvent &e) {
            const auto &m=e.message; const int status=quint8(m[0]);
            if(status==0xf0) {
                synth->playSysexNow(reinterpret_cast<const MT32Emu::Bit8u *>(m.constData()),MT32Emu::Bit32u(m.size()));
                readRoutes(); changed=true;
            } else if((status&0xf0)==0x90 && m.size()==3 && quint8(m[2])) used |= routes[status&15];
            return routes[status&15];
        });
        if(changed) reset(); // Analysis must not leave later patches/routing in the playback synth.
        return VoiceMask::first(9) & ~used;
    }
    void refill(VoiceMask mute) {
        input={}; captureStart=synth->getInternalRenderedSampleCount();
        synth->bitmusicMuteMask=0;
        for(int i=0;i<9;++i) if(mute&VoiceMask::bit(i)) synth->bitmusicMuteMask|=1u<<i;
        int done=0;
        const auto &events=resources->sequence.events;
        while(done<Chunk) {
            while(event<events.size() && events[event].frame*Rate/SampleRate<=generated) {
                if(!enqueue(events[event].message)) break; // Drain a full queue, then retry without losing the event.
                ++event;
            }
            int n=Chunk-done;
            if(event<events.size()) n=int(std::clamp<qint64>(events[event].frame*Rate/SampleRate-generated,1,n));
            std::array<float,2*Chunk> audio{};
            synth->render(audio.data(),MT32Emu::Bit32u(n));
            for(int i=0;i<n;++i) { input[done+i][0]=audio[2*i]; input[done+i][1]=audio[2*i+1]; }
            done+=n; generated+=n; rememberNames(generated*SampleRate/Rate);
        }
        inputAt=0;
    }
    // Fast seek restores MIDI state, then renders a short preroll. Older held
    // notes restart using the current patch: exact envelope/phase/reverb recovery
    // would require rendering all preceding audio (Munt has no state snapshots).
    bool chase(qint64 start,const std::function<bool()> &cancelled) {
        struct Note { int velocity=0; bool down=false; };
        std::array<std::array<Note,128>,16> notes{};
        std::array<bool,16> sustain{};
        const auto release=[&](int ch) { for(auto &n:notes[ch]) if(!n.down&&!sustain[ch]) n={}; };
        const auto &events=resources->sequence.events;
        while(event<events.size() && events[event].frame<start) {
            if(cancelled()) return false;
            const auto &m=events[event++].message;
            const int status=quint8(m[0]),kind=status&0xf0,ch=status&15;
            const int a=m.size()>1?quint8(m[1]):0,b=m.size()>2?quint8(m[2]):0;
            if(kind==0x90 && b) notes[ch][a]={b,true};
            else if(kind==0x80||kind==0x90) { notes[ch][a].down=false; release(ch); }
            else {
                if(kind==0xc0) {
                    // MT-32 Program Change silences melodic parts and clears
                    // their hold pedal (unlike GM). Respect current SysEx routing.
                    MT32Emu::Bit8u assignment[9]{};
                    synth->readMemory((0x10u<<14)+13,9,assignment); // System: 10 00 0D.
                    if(std::find(assignment,assignment+8,ch)!=assignment+8) { notes[ch]={}; sustain[ch]=false; }
                }
                if(kind==0xb0) {
                    if(a==64) { sustain[ch]=b>=64; release(ch); }
                    if(a==121) { sustain[ch]=false; release(ch); }
                    if(a==123||a>=124) { if(a>=124) sustain[ch]=false; for(auto &n:notes[ch]) n.down=false; release(ch); }
                }
                if(status==0xf0) synth->playSysexNow(reinterpret_cast<const MT32Emu::Bit8u *>(m.constData()),MT32Emu::Bit32u(m.size()));
                else synth->playMsgNow(shortMessage(m));
                if(resetReported) { notes={}; sustain={}; resetReported=false; }
            }
        }
        for(int ch=0;ch<16;++ch) for(int key=0;key<128;++key) if(notes[ch][key].velocity) {
            require(synth->playMsg(MT32Emu::Bit32u(0x90|ch)|(MT32Emu::Bit32u(key)<<8)|(MT32Emu::Bit32u(notes[ch][key].velocity)<<16)),"MT-32 seek queue overflow.");
            if(!notes[ch][key].down) require(synth->playMsg(MT32Emu::Bit32u(0x80|ch)|(MT32Emu::Bit32u(key)<<8)),"MT-32 seek queue overflow.");
        }
        generated=consumed=start*Rate/SampleRate; names.clear(); rememberNames(start);
        return !cancelled();
    }
};
Mt32Track::Mt32Track(const QString &path,QByteArray data,int song,bool playlist,bool capture,std::shared_ptr<const Mt32Resources> resources):impl_(std::make_unique<Impl>()) {
    require(song==0,"MIDI contains one song; its tracks play together.");
    auto &d=*impl_; d.resources=resources?std::move(resources):loadResources(data); d.capture=capture;
    info_.mt32Resources=d.resources; info_.path=path; info_.songs=1; info_.system=d.resources->model;
    info_.title=d.resources->sequence.title; if(info_.title.isEmpty()) info_.title=QFileInfo(path).completeBaseName();
    info_.durationMs=int((d.resources->sequence.endFrame+TailSeconds*SampleRate)*1000/SampleRate);
    info_.stereoOutput=true; info_.tonalMask=VoiceMask(255);
    auto &c=info_.configuration; c.family="midi-mt32"; c.name="LA32";
    for(int i=0;i<9;++i) { info_.voices<< (i==8?"Rhythm":QString("Part %1").arg(i+1)); c.sources<<QString("mt32.part:%1").arg(i); }
    c.id=QString::fromLatin1(QCryptographicHash::hash("midi-mt32|9-part-v1",QCryptographicHash::Sha256).toHex());
    if(playlist) info_.playlist.push_back({info_.title,info_.durationMs,0});
    d.reset();
    info_.emptyVoices=d.emptyParts(info_.noteRoll);
}
Mt32Track::~Mt32Track()=default;
bool Mt32Track::hasTaps() const { return impl_->capture; }
void Mt32Track::render(short *out,int frames) {
    auto &d=*impl_;
    for(int i=0;i<frames;++i,++frame_) {
        const qint64 position=(frame_+1)*Rate, target=position/SampleRate;
        while(d.consumed<target) {
            if(d.inputAt==Chunk) d.refill(muteMask_);
            d.filterHistory[d.cursor]=d.input[d.inputAt++]; d.cursor=(d.cursor+1)%Taps; ++d.consumed;
        }
        const int phase=int((position%SampleRate)*Phases/SampleRate),lanes=d.capture?20:2;
        Sample result{};
        for(int t=0;t<Taps;++t) {
            const auto &s=d.filterHistory[(d.cursor+Taps-1-t)%Taps]; const float f=d.filter[phase][t];
            for(int c=0;c<lanes;++c) result[c]+=s[c]*f;
        }
        for(int side=0;side<2;++side) *out++=short(std::clamp(std::lround(result[side]*32768.f),-32768L,32767L));
        if(d.capture) for(int ch=0;ch<9;++ch) for(int side=0;side<2;++side)
            d.history[ch][side][frame_%ScopeFrames]=result[2+2*ch+side]*32768.f;
    }
}
bool Mt32Track::seek(qint64 target,const std::function<bool()> &cancelled) {
    target=std::clamp<qint64>(target,0,qint64(info_.durationMs)*SampleRate/1000);
    if(cancelled()) return false;
    if(target<frame_ || target-frame_>2*SampleRate) {
        impl_->reset(); frame_=0;
        const auto start=std::max<qint64>(0,target-2*SampleRate);
        if(start) { if(!impl_->chase(start,cancelled)) { impl_->reset(); return false; } frame_=start; }
    }
    std::array<short,2*BlockFrames> scratch{};
    while(frame_<target) { if(cancelled()) return false; render(scratch.data(),int(std::min<qint64>(BlockFrames,target-frame_))); }
    return !cancelled();
}
QStringList Mt32Track::instrumentNames(qint64 frame) const {
    const auto &names=impl_->names;
    const auto at=std::upper_bound(names.begin(),names.end(),frame,[](qint64 f,const Impl::Names &n){return f<n.frame;});
    return at==names.begin()?QStringList{}:std::prev(at)->parts;
}
bool Mt32Track::readTap(int channel,QVector<float> &left,QVector<float> &right) const {
    if(!impl_->capture||channel<0||channel>=9) return false;
    left.fill(0,ScopeFrames); right.fill(0,ScopeFrames);
    for(int i=0;i<ScopeFrames;++i) { const auto at=frame_-ScopeFrames+i;
        if(at>=0) {left[i]=impl_->history[channel][0][at%ScopeFrames]; right[i]=impl_->history[channel][1][at%ScopeFrames];} }
    return true;
}
QVector<float> Mt32Track::notePitches(int channel) const {
    if(channel<0||channel>=8) return {-1};
    MT32Emu::Bit8u keys[32]{},velocities[32]{};
    const auto count=impl_->synth->getPlayingNotes(MT32Emu::Bit8u(channel),keys,velocities);
    QVector<float> result;
    for(unsigned i=0;i<count;++i) { const auto hz=float(440*std::pow(2.,(int(keys[i])-69)/12.)); if(!result.contains(hz)) result<<hz; }
    std::sort(result.begin(),result.end()); return result;
}

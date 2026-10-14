#include "miditrack.h"
#include "appsettings.h"
#include <fluidsynth.h>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <stdexcept>

namespace {
void require(bool ok,const char *s) { if (!ok) throw std::runtime_error(s); }
constexpr int Polyphony=256, Chunk=64, TailSeconds=2;
constexpr qint64 SeekPreroll=2*SampleRate;
thread_local std::shared_ptr<const MidiResources> loading;
struct Cursor { std::shared_ptr<const MidiResources> resource; qint64 pos=0; };
void *sfOpen(const char *name) {
    if (!loading || loading->soundFontKey != name) return nullptr;
    return new Cursor{loading};
}
int sfRead(void *out,fluid_long_long_t count,void *handle) {
    auto &c=*static_cast<Cursor *>(handle); const auto &b=c.resource->soundFont;
    if (count<0 || count>b.size()-c.pos) return FLUID_FAILED;
    std::memcpy(out,b.constData()+c.pos,size_t(count)); c.pos+=count; return FLUID_OK;
}
int sfSeek(void *handle,fluid_long_long_t offset,int origin) {
    auto &c=*static_cast<Cursor *>(handle); const auto size=c.resource->soundFont.size();
    const qint64 base=origin==SEEK_SET?0:origin==SEEK_CUR?c.pos:origin==SEEK_END?size:-1;
    if (base<0 || offset < -base || offset>size-base) return FLUID_FAILED;
    c.pos=base+offset; return FLUID_OK;
}
fluid_long_long_t sfTell(void *handle) { return static_cast<Cursor *>(handle)->pos; }
int sfClose(void *handle) { delete static_cast<Cursor *>(handle); return FLUID_OK; }
std::shared_ptr<const MidiResources> loadResources(const QByteArray &data) {
    auto r=std::make_shared<MidiResources>(); r->sequence=MidiSequence::parse(data,SampleRate);
    require(!r->sequence.mt32,"This file contains Roland MT-32 SysEx. Select Auto or MT-32; General MIDI cannot reproduce these patches.");
    r->soundFontName=selectedSoundFont();
    QFile file(QDir(playerSoundFontsDirectory()).filePath(r->soundFontName));
    if (r->soundFontName.isEmpty() || !file.open(QIODevice::ReadOnly))
        throw std::runtime_error(("SoundFont required: place a .sf2 file in:\n" +
            QDir::toNativeSeparators(playerSoundFontsDirectory())+"\nThen select it in the player's SoundFont list and reopen the MIDI.").toStdString());
    require(file.size()>12 && file.size()<=1024LL*1024*1024,"Invalid SoundFont size (maximum 1 GiB).");
    r->soundFont=file.readAll();
    require(file.error()==QFile::NoError && r->soundFont.size()==file.size(),"Cannot read SoundFont.");
    require(r->soundFont.startsWith("RIFF") && r->soundFont.mid(8,4)=="sfbk","Not an SF2 SoundFont.");
    r->soundFontKey="bitmusic-memory-"+QCryptographicHash::hash(r->soundFont,QCryptographicHash::Sha256).toHex()+".sf2";
    return r;
}
}
QString playerSoundFontsDirectory() {
    const auto test=QCoreApplication::instance()->property("soundFontsDirectoryForTests").toString();
    if (!test.isEmpty()) return test;
    return QDir::cleanPath(QDir(playerThemesDirectory()).absoluteFilePath("../soundfonts"));
}
QStringList availableSoundFonts() {
    QStringList names;
    for (const auto &name:QDir(playerSoundFontsDirectory()).entryList(QDir::Files,QDir::Name))
        if (name.endsWith(".sf2",Qt::CaseInsensitive)) names<<name;
    return names;
}
QString selectedSoundFont() {
    QSettings s(playerSettingsPath(),playerSettingsFormat()); s.setFallbacksEnabled(false);
    const auto name=s.value("MIDI/SoundFont").toString();
    const auto names=availableSoundFonts();
    // A selected missing bank must not silently switch the arrangement's sound.
    if (!name.isEmpty() && QFileInfo(name).fileName()==name && !name.contains('/') && !name.contains('\\')) return name;
    return names.value(0);
}
void selectSoundFont(const QString &name) {
    require(availableSoundFonts().contains(name),"Selected SoundFont is not available.");
    QSettings s(playerSettingsPath(),playerSettingsFormat()); s.setFallbacksEnabled(false);
    s.setValue("MIDI/SoundFont",name); s.sync();
    require(s.status()==QSettings::NoError,"Could not save the selected SoundFont.");
}
struct MidiTrack::Impl {
    std::shared_ptr<const MidiResources> resources;
    std::unique_ptr<fluid_settings_t,decltype(&delete_fluid_settings)> settings{nullptr,delete_fluid_settings};
    std::unique_ptr<fluid_synth_t,decltype(&delete_fluid_synth)> synth{nullptr,delete_fluid_synth};
    std::array<std::array<float,Chunk>,32> audio{};
    std::array<std::array<float,Chunk>,64> effects{};
    std::vector<std::array<std::array<float,ScopeFrames>,2>> history;
    qsizetype event=0; bool released=false, capture=false;
    struct Names { qint64 frame; QStringList channels; };
    std::deque<Names> names;
    void rememberNames(qint64 frame) {
        QStringList channels;
        for(int ch=0;ch<16;++ch) {
            auto *preset=fluid_synth_get_channel_preset(synth.get(),ch);
            channels<< (preset ? QString::fromUtf8(fluid_preset_get_name(preset)).trimmed() : QString());
        }
        if(names.empty() || channels!=names.back().channels) names.push_back({frame,channels});
        // Only the audio ring's read-ahead needs history. Keep its preceding state too.
        while(names.size()>1 && names[1].frame<frame-SampleRate) names.pop_front();
    }
    void reset() {
        synth.reset(); settings.reset(new_fluid_settings()); require(bool(settings),"Cannot create MIDI settings.");
        auto *s=settings.get();
        fluid_settings_setnum(s,"synth.sample-rate",SampleRate);
        fluid_settings_setnum(s,"synth.gain",0.5);
        fluid_settings_setint(s,"synth.polyphony",Polyphony);
        fluid_settings_setint(s,"synth.midi-channels",16);
        fluid_settings_setint(s,"synth.audio-channels",16);
        fluid_settings_setint(s,"synth.audio-groups",16);
        // Independent effect buses let mute/Solo preserve synthesis and existing
        // effect tails without leaking a muted channel through a shared return.
        fluid_settings_setint(s,"synth.effects-groups",16);
        fluid_settings_setint(s,"synth.reverb.active",!capture);
        fluid_settings_setint(s,"synth.chorus.active",!capture);
        fluid_settings_setint(s,"synth.threadsafe-api",0);
        fluid_settings_setint(s,"synth.cpu-cores",1);
        fluid_settings_setint(s,"synth.dynamic-sample-loading",0);
        fluid_settings_setstr(s,"synth.midi-bank-select","gm");
        synth.reset(new_fluid_synth(s)); require(bool(synth),"Cannot create General MIDI synthesizer.");
        auto *loader=new_fluid_defsfloader(s); require(loader,"Cannot create SoundFont loader.");
        fluid_sfloader_set_callbacks(loader,sfOpen,sfRead,sfSeek,sfTell,sfClose);
        fluid_synth_add_sfloader(synth.get(),loader);
        loading=resources;
        const int id=fluid_synth_sfload(synth.get(),resources->soundFontKey.constData(),1);
        loading.reset();
        require(id>=0,"Cannot load SoundFont. Use a valid uncompressed SF2 file.");
        event=0; released=false;
        names.clear(); rememberNames(0);
        if (capture) history.assign(16,{});
    }
    void dispatch(const QByteArray &m) {
        auto *s=synth.get(); const int status=quint8(m[0]), ch=status&15;
        const int a=m.size()>1?quint8(m[1]):0, b=m.size()>2?quint8(m[2]):0;
        switch(status&0xf0) {
        case 0x80: fluid_synth_noteoff(s,ch,a); break;
        case 0x90: if(b) fluid_synth_noteon(s,ch,a,b); else fluid_synth_noteoff(s,ch,a); break;
        case 0xa0: fluid_synth_key_pressure(s,ch,a,b); break;
        case 0xb0: fluid_synth_cc(s,ch,a,b); break;
        case 0xc0: fluid_synth_program_change(s,ch,a); break;
        case 0xd0: fluid_synth_channel_pressure(s,ch,a); break;
        case 0xe0: fluid_synth_pitch_bend(s,ch,a|(b<<7)); break;
        case 0xf0: fluid_synth_sysex(s,m.constData()+1,int(m.size())-2,nullptr,nullptr,nullptr,0); break;
        }
    }
    bool chase(qint64 target,const std::function<bool()> &cancelled) {
        // Reconstruct MIDI state without rendering the entire elapsed soundtrack.
        // Old held notes are restarted; sample phase/envelopes and long effect tails
        // cannot be restored through FluidSynth's public API. A short render follows.
        struct Note { int velocity=0, font=0, bank=0, program=0; bool down=false, sostenuto=false; };
        std::array<std::array<Note,128>,16> notes{};
        std::array<std::array<int,128>,16> pressure{};
        std::array<bool,16> sustain{}, sostenuto{};
        auto *s=synth.get();
        const auto release=[&](int ch) {
            for(auto &n:notes[ch]) if(!n.down && !sustain[ch] && !n.sostenuto) n={};
        };
        const auto &events=resources->sequence.events;
        while(event<events.size() && events[event].frame<target) {
            if(cancelled()) return false;
            const auto &e=events[event++]; const auto &m=e.message;
            const int status=quint8(m[0]), kind=status&0xf0, ch=status&15;
            const int a=m.size()>1?quint8(m[1]):0, b=m.size()>2?quint8(m[2]):0;
            if(kind==0x90 && b) {
                auto &n=notes[ch][a]; n={}; n.velocity=b; n.down=true;
                if(auto *p=fluid_synth_get_channel_preset(s,ch)) {
                    n.font=fluid_sfont_get_id(fluid_preset_get_sfont(p));
                    n.bank=fluid_preset_get_banknum(p); n.program=fluid_preset_get_num(p);
                } else n={};
            } else if(kind==0x80 || kind==0x90) {
                notes[ch][a].down=false; release(ch);
            } else {
                if(kind==0xa0) pressure[ch][a]=b;
                if(kind==0xb0) {
                    if(a==64) { sustain[ch]=b>=64; release(ch); }
                    if(a==66) {
                        if((b>=64)!=sostenuto[ch]) {
                            sostenuto[ch]=b>=64;
                            for(auto &n:notes[ch]) n.sostenuto=sostenuto[ch]&&n.down;
                        }
                        release(ch);
                    }
                    if(a==120) notes[ch]={};
                    if(a==121) {
                        sustain[ch]=sostenuto[ch]=false;
                        pressure[ch]={};
                        for(auto &n:notes[ch]) n.sostenuto=false;
                        release(ch);
                    }
                    if(a==123 || a>=124) { for(auto &n:notes[ch]) n.down=false; release(ch); }
                }
                // Match the reset messages handled by FluidSynth. Ask its dry-run
                // parser first so invalid checksums/device IDs cannot clear notes.
                if(status==0xf0) {
                    int handled=0;
                    const bool accepted=fluid_synth_sysex(s,m.constData()+1,int(m.size())-2,nullptr,nullptr,&handled,1)==FLUID_OK && handled;
                    const bool gm=m.size()==6 && a==0x7e && quint8(m[3])==9 && (quint8(m[4])==1 || quint8(m[4])==3);
                    const bool gs=m.size()==11 && a==0x41 && m.mid(3,5)==QByteArray::fromHex("421240007f");
                    const bool xg=m.size()==9 && a==0x43 && m.mid(3,3)==QByteArray::fromHex("4c0000") && (quint8(m[6])==0x7e || quint8(m[6])==0x7f);
                    if(accepted && (gm||gs||xg)) { notes={}; pressure={}; sustain={}; sostenuto={}; }
                }
                dispatch(m);
                if(kind==0xc0 || status==0xf0) rememberNames(e.frame);
            }
        }
        for(int ch=0;ch<16;++ch) {
            if(std::none_of(notes[ch].begin(),notes[ch].end(),[](const Note &n){return n.velocity!=0;})) continue;
            int font=0,bank=0,pedal=0,hold=0;
            fluid_synth_get_program(s,ch,&font,&bank,nullptr);
            auto *preset=fluid_synth_get_channel_preset(s,ch);
            const int actualFont=preset?fluid_sfont_get_id(fluid_preset_get_sfont(preset)):-1;
            const int actualBank=preset?fluid_preset_get_banknum(preset):0;
            const int actualProgram=preset?fluid_preset_get_num(preset):0;
            fluid_synth_get_cc(s,ch,64,&pedal); fluid_synth_get_cc(s,ch,66,&hold);
            fluid_synth_cc(s,ch,64,0); fluid_synth_cc(s,ch,66,0);
            // Latch only the keys that were held when sostenuto was pressed.
            for(bool latched:{true,false}) {
                for(int key=0;key<128;++key) {
                    const auto &n=notes[ch][key];
                    if(n.velocity && n.sostenuto==latched) {
                        fluid_synth_program_select(s,ch,n.font,n.bank,n.program);
                        fluid_synth_noteon(s,ch,key,n.velocity);
                        fluid_synth_key_pressure(s,ch,key,pressure[ch][key]);
                    }
                }
                if(latched) fluid_synth_cc(s,ch,66,hold);
            }
            fluid_synth_cc(s,ch,64,pedal);
            for(int key=0;key<128;++key) if(notes[ch][key].velocity && !notes[ch][key].down) fluid_synth_noteoff(s,ch,key);
            if(preset) fluid_synth_program_select(s,ch,actualFont,actualBank,actualProgram);
            else fluid_synth_unset_program(s,ch);
            // Bank Select may be waiting for a future Program Change. Keep it
            // separate from the actual preset (including SoundFont fallbacks).
            fluid_synth_bank_select(s,ch,bank); fluid_synth_sfont_select(s,ch,font);
        }
        rememberNames(target);
        return !cancelled();
    }
};
MidiTrack::MidiTrack(const QString &path,QByteArray data,int song,bool playlist,bool capture,
                     std::shared_ptr<const MidiResources> resources):impl_(std::make_unique<Impl>()) {
    require(song==0,"MIDI contains one song; its tracks play together.");
    const bool fresh=!resources;
    auto &d=*impl_; d.resources=resources?std::move(resources):loadResources(data); d.capture=capture;
    info_.midiResources=d.resources; info_.path=path; info_.songs=1; info_.system="General MIDI";
    info_.title=d.resources->sequence.title;
    if(info_.title.isEmpty()) info_.title=QFileInfo(path).completeBaseName();
    info_.durationMs=int((d.resources->sequence.endFrame+TailSeconds*SampleRate)*1000/SampleRate);
    info_.stereoOutput=true; info_.tonalMask=VoiceMask(0xffffu & ~(1u<<9));
    auto &c=info_.configuration; c.family="midi-gm"; c.name="FluidSynth";
    for(int i=0;i<16;++i) {
        info_.voices<< (i==9?QString("Channel 10 / Drums"):QString("Channel %1").arg(i+1));
        c.sources<<QString("midi.channel:%1").arg(i);
    }
    c.id=QString::fromLatin1(QCryptographicHash::hash("midi-gm|16-channel-v1",QCryptographicHash::Sha256).toHex());
    if(playlist) info_.playlist.push_back({info_.title,info_.durationMs,0});
    d.reset();
    if(fresh) selectSoundFont(d.resources->soundFontName);
}
MidiTrack::~MidiTrack()=default;
bool MidiTrack::hasTaps() const { return impl_->capture; }
void MidiTrack::render(short *out,int frames) {
    auto &d=*impl_; const auto &sequence=d.resources->sequence;
    while(frames>0) {
        while(d.event<sequence.events.size() && sequence.events[d.event].frame<=frame_) {
            const auto &e=sequence.events[d.event++]; d.dispatch(e.message);
            const auto status=quint8(e.message[0]);
            if((status&0xf0)==0xc0 || status==0xf0) d.rememberNames(e.frame);
        }
        if(!d.released && frame_>=sequence.endFrame) {
            for(int ch=0;ch<16;++ch) { fluid_synth_cc(d.synth.get(),ch,64,0); fluid_synth_cc(d.synth.get(),ch,66,0); fluid_synth_all_notes_off(d.synth.get(),ch); }
            d.released=true;
        }
        int count=std::min(frames,Chunk);
        if(d.event<sequence.events.size()) count=int(std::min<qint64>(count,sequence.events[d.event].frame-frame_));
        if(!d.released) count=int(std::min<qint64>(count,sequence.endFrame-frame_));
        require(count>0,"Invalid MIDI event timeline.");
        float *audio[32],*fx[64];
        for(int c=0;c<32;++c) { d.audio[c].fill(0); audio[c]=d.audio[c].data(); }
        for(int c=0;c<64;++c) { d.effects[c].fill(0); fx[c]=d.effects[c].data(); }
        require(fluid_synth_process(d.synth.get(),count,d.capture?0:64,d.capture?nullptr:fx,32,audio)==FLUID_OK,"MIDI synthesis failed.");
        for(int i=0;i<count;++i) {
            float mix[2]{};
            for(int c=0;c<16;++c) for(int side=0;side<2;++side) {
                const float sample=audio[2*c+side][i];
                if(d.capture) d.history[c][side][(frame_+i)%ScopeFrames]=sample*32768.f;
                if(!(muteMask_&VoiceMask::bit(c))) mix[side]+=sample+fx[4*c+side][i]+fx[4*c+2+side][i];
            }
            for(float v:mix) *out++=short(std::clamp(std::lround(v*32768.f),-32768L,32767L));
        }
        frame_+=count; frames-=count;
    }
}
bool MidiTrack::seek(qint64 target,const std::function<bool()> &cancelled) {
    target=std::clamp<qint64>(target,0,qint64(info_.durationMs)*SampleRate/1000);
    if(cancelled()) return false;
    if(target<frame_ || target-frame_>SeekPreroll) {
        impl_->reset(); frame_=0;
        const auto start=std::max<qint64>(0,target-SeekPreroll);
        if(start) {
            if(!impl_->chase(start,cancelled)) { impl_->reset(); return false; }
            frame_=start;
        }
    }
    std::array<short,BlockFrames*2> scratch;
    while(frame_<target) { if(cancelled()) return false; render(scratch.data(),int(std::min<qint64>(BlockFrames,target-frame_))); }
    return !cancelled();
}
QStringList MidiTrack::instrumentNames(qint64 frame) const {
    const auto &names=impl_->names;
    auto at=std::upper_bound(names.begin(),names.end(),frame,[](qint64 f,const Impl::Names &n){return f<n.frame;});
    return at==names.begin() ? QStringList{} : std::prev(at)->channels;
}
bool MidiTrack::readTap(int channel,QVector<float>&left,QVector<float>&right) const {
    if(!impl_->capture || channel<0 || channel>=16) return false;
    left.fill(0,ScopeFrames); right.fill(0,ScopeFrames);
    for(int i=0;i<ScopeFrames;++i) {
        const auto at=frame_-ScopeFrames+i;
        if(at>=0) { left[i]=impl_->history[channel][0][at%ScopeFrames]; right[i]=impl_->history[channel][1][at%ScopeFrames]; }
    }
    return true;
}
QVector<float> MidiTrack::notePitches(int channel) const {
    if(channel<0 || channel>=16 || channel==9) return {-1};
    std::array<fluid_voice_t *,Polyphony> voices{};
    fluid_synth_get_voicelist(impl_->synth.get(),voices.data(),Polyphony,-1);
    int bend=8192,sensitivity=2;
    fluid_synth_get_pitch_bend(impl_->synth.get(),channel,&bend);
    fluid_synth_get_pitch_wheel_sens(impl_->synth.get(),channel,&sensitivity);
    QVector<float> result;
    for(auto *voice:voices) if(voice && fluid_voice_get_channel(voice)==channel &&
        (fluid_voice_is_on(voice)||fluid_voice_is_sustained(voice)||fluid_voice_is_sostenuto(voice))) {
        const auto hz=float(440*std::pow(2.0,(fluid_voice_get_key(voice)-69+(bend-8192)*sensitivity/8192.0)/12.0));
        if(!result.contains(hz)) result<<hz;
    }
    std::sort(result.begin(),result.end()); return result;
}

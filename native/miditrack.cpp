#include "miditrack.h"
#include "appsettings.h"
#include <fluidsynth.h>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
void require(bool ok,const char *s) { if (!ok) throw std::runtime_error(s); }
constexpr int Polyphony=256, Chunk=64, TailSeconds=2;
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
    require(!r->sequence.mt32,"This file contains Roland MT-32 SysEx. MT-32 playback is the next milestone; General MIDI cannot reproduce these patches.");
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
        while(d.event<sequence.events.size() && sequence.events[d.event].frame<=frame_) d.dispatch(sequence.events[d.event++].message);
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
    if(target<frame_) { impl_->reset(); frame_=0; }
    std::array<short,BlockFrames*2> scratch;
    while(frame_<target) { if(cancelled()) return false; render(scratch.data(),int(std::min<qint64>(BlockFrames,target-frame_))); }
    return !cancelled();
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

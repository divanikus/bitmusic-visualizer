#include "midisequence.h"
#include <array>
#include <algorithm>

std::shared_ptr<const NoteRoll> midiNoteRoll(const MidiSequence &sequence,
    const std::function<VoiceMask(const MidiEvent &)> &routing) {
    auto roll=std::make_shared<NoteRoll>();
    std::array<std::array<QVector<int>,128>,16> down;
    std::array<int,16> programs{},banks{};
    const auto finish=[&](int ch,int key,qint64 frame) {
        for(int id:down[ch][key]) roll->notes[id].end=std::max(roll->notes[id].start+1,frame);
        down[ch][key].clear();
    };
    for(const auto &e:sequence.events) {
        const auto &m=e.message; if(m.isEmpty()) continue;
        const int status=quint8(m[0]),kind=status&0xf0,ch=status&15;
        const auto routes=routing?routing(e):VoiceMask::bit(ch);
        if(status>=0xf0) continue;
        const int a=m.size()>1?quint8(m[1]):0,b=m.size()>2?quint8(m[2]):0;
        if(kind==0xc0) programs[ch]=a;
        else if(kind==0xb0 && a==0) banks[ch]=b;
        else if(kind==0x90 && b) {
            // Repeated keys replace their prior gate, matching our MIDI backends.
            finish(ch,a,e.frame);
            for(int source=0;source<16;++source) if(routes.test(source)) {
                down[ch][a]<<int(roll->notes.size());
                roll->notes.push_back({e.frame,std::max(e.frame+1,sequence.endFrame),source,a,
                    ch==9?16384+a:programs[ch]+128*banks[ch],b});
            }
        } else if(kind==0x80 || kind==0x90) finish(ch,a,e.frame);
        else if(kind==0xb0 && (a==120||a>=123)) for(int key=0;key<128;++key) finish(ch,key,e.frame);
    }
    roll->index(); return roll;
}

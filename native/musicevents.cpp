#include "musicevents.h"
#include <algorithm>
#include <functional>

void NoteRoll::index() {
    std::stable_sort(notes.begin(),notes.end(),[](const auto &a,const auto &b){return a.start<b.start;});
    maxEnds.fill(0,notes.size()*4);
    if(notes.isEmpty()) return;
    low=127; high=0;
    for(const auto &n:notes) {low=std::min(low,n.key); high=std::max(high,n.key);}
    // Stable range for the whole song; never jump octaves during playback.
    low=std::max(0,(low/12)*12); high=std::min(127,(high/12+1)*12-1);
    if(high-low<23) { low=std::max(0,low-12); high=std::min(127,std::max(high,low+23)); }
    const std::function<qint64(int,int,int)> build=[&](int node,int a,int b) {
        return maxEnds[node]=b-a==1?notes[a].end:std::max(build(node*2+1,a,(a+b)/2),build(node*2+2,(a+b)/2,b));
    };
    build(0,0,int(notes.size()));
}
QVector<int> NoteRoll::visible(qint64 from,qint64 to,int source,int limit) const {
    QVector<int> result;
    if(notes.isEmpty()||maxEnds.isEmpty()||limit<=0) return result;
    const std::function<void(int,int,int)> visit=[&](int node,int a,int b) {
        if(result.size()>=limit||maxEnds[node]<=from||notes[a].start>to) return;
        if(b-a==1) {if(source<0||notes[a].source==source) result<<a; return;}
        visit(node*2+1,a,(a+b)/2); visit(node*2+2,(a+b)/2,b);
    };
    visit(0,0,int(notes.size())); return result;
}
QString trackerNoteName(int note) {
    if(note==0x81) return "OFF";
    if(note==0x82) return "CUT";
    if(note==0x83) return "FAD";
    if(note<1||note>120) return "...";
    static const char *names[]={"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
    return QString::fromLatin1(names[(note-1)%12])+QString::number((note-1)/12);
}

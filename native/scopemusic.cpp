#include "scopewindow.h"
#include <QDataStream>
#include <QFontDatabase>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
bool blackKey(int n) {const int k=n%12; return k==1||k==3||k==6||k==8||k==10;}
QString hex(int n,int width=2) {return QString::number(n,16).toUpper().rightJustified(width,'0');}
}

void ScopeWindow::paintMusic(QPainter &p,int card,const QRectF &plot) {
    p.save(); p.setClipRect(plot,Qt::IntersectClip);
    p.fillRect(plot,theme_.colors(sourceFor(card)).background);
    if(state_.busy || !viewSupported(viewFor(card))) {
        p.setPen(theme_.colors(sourceFor(card)).label);
        p.drawText(plot,Qt::AlignCenter,state_.busy?"Seeking...":"View unavailable");
    } else if(viewFor(card)==ViewKind::PianoRoll) paintPianoRoll(p,card,plot);
    else paintPattern(p,card,plot);
    p.restore();
}

void ScopeWindow::paintPianoRoll(QPainter &p,int card,const QRectF &plot) {
    const auto &roll=*state_.info.noteRoll;
    const auto &colors=theme_.colors(sourceFor(card));
    const bool horizontal=cards_[card].horizontal;
    const int source=sourceFor(card)==ScopeTheme::FullMix?-1:sourceFor(card);
    // Keep the familiar piano range even in sparse tracks; include outliers.
    const int lo=std::min(21,roll.low),hi=std::max(108,roll.high);
    int whites=0; for(int n=lo;n<=hi;++n) if(!blackKey(n)) ++whites;
    if(!whites) return;
    const double cross=horizontal?plot.height():plot.width();
    const double length=horizontal?plot.width():plot.height();
    const double keyDepth=length*cards_[card].keyboardSize/100., travel=length-keyDepth;
    if(travel<8||cross<8) return;
    const double whiteWidth=cross/whites;
    std::array<QRectF,128> keys;
    int white=0;
    for(int n=lo;n<=hi;++n) {
        const bool black=blackKey(n); const double x=(white-(black?.32:0))*whiteWidth;
        keys[n]=QRectF(x,0,whiteWidth*(black?.64:1),keyDepth*(black?.62:1));
        if(!black) ++white;
    }
    // Logical coordinates: pitch along X, notes fall along Y toward the keys.
    // Reflow to horizontal without rotating labels or glyphs.
    const auto map=[&](const QRectF &r) {
        return horizontal?QRectF(plot.left()+length-r.bottom(),plot.top()+cross-r.right(),r.height(),r.width())
                         :r.translated(plot.topLeft());
    };
    const qint64 now=qint64(state_.positionMs)*SampleRate/1000;
    constexpr qint64 Ahead=4LL*SampleRate;
    const double scale=travel/Ahead;
    p.setPen(QPen(colors.axis,.7));
    for(int n=lo;n<=hi;++n) if(!blackKey(n)) {
        const auto rect=map({keys[n].left(),0,keys[n].width(),travel});
        if(horizontal) p.drawLine(rect.bottomLeft(),rect.bottomRight());
        else p.drawLine(rect.topLeft(),rect.bottomLeft());
    }
    for(qint64 frame=(now/SampleRate+1)*SampleRate;frame<now+Ahead;frame+=SampleRate) {
        const auto rect=map({0,travel-(frame-now)*scale,cross,0});
        p.drawLine(rect.topLeft(),horizontal?rect.bottomLeft():rect.topRight());
    }
    std::array<QColor,128> down;
    const auto visible=roll.visible(now,now+Ahead,source);
    for(int id:visible) {
        const auto &n=roll.notes[id]; if(n.key<lo||n.key>hi) continue;
        auto color=theme_.colors(n.instrument%ScopeTheme::SlotCount).waveform;
        if(state_.muteMask.test(n.source)||!state_.outputMask) color.setAlpha(85);
        else color.setAlpha(150+n.velocity*80/127);
        const auto &key=keys[n.key];
        const double top=std::max(0.,travel-(n.end-now)*scale);
        const double bottom=std::min(travel,travel-(n.start-now)*scale);
        const auto rect=map({key.left()+.6,top,std::max(.5,key.width()-1.2),std::max(1.,bottom-top)});
        p.setPen(Qt::NoPen); p.setBrush(color); p.drawRoundedRect(rect,2,2);
        if(n.start<=now&&n.end>now) down[n.key]=color;
    }
    for(int pass=0;pass<2;++pass) for(int n=lo;n<=hi;++n) if(blackKey(n)==bool(pass)) {
        const auto key=keys[n];
        const auto rect=map({key.left(),travel,key.width(),key.height()});
        QColor fill=pass?QColor("#26343a"):QColor("#cdd8da");
        if(down[n].isValid()) {
            // Composite dimmed voices onto the key rather than leaving a hole.
            const auto ink=down[n]; const double alpha=ink.alphaF();
            fill.setRgbF(fill.redF()*(1-alpha)+ink.redF()*alpha,fill.greenF()*(1-alpha)+ink.greenF()*alpha,fill.blueF()*(1-alpha)+ink.blueF()*alpha);
        }
        p.setPen(QPen(colors.background,.8)); p.setBrush(fill); p.drawRoundedRect(rect.adjusted(.3,.3,-.3,-.3),1.5,1.5);
        if(!pass&&n%12==0&&whiteWidth>=14&&keyDepth>=26) {
            p.setFont(QFont("Segoe UI",7)); p.setPen(QColor("#26343a"));
            p.drawText(rect.adjusted(2,1,-1,-2),horizontal?Qt::AlignLeft|Qt::AlignVCenter:Qt::AlignHCenter|Qt::AlignBottom,QString("C%1").arg(n/12-1));
        }
    }
    p.setPen(QPen(colors.waveform,1.3));
    const auto line=map({0,travel,cross,0}); p.drawLine(line.topLeft(),horizontal?line.bottomLeft():line.topRight());
    if(roll.notes.isEmpty()) {p.setPen(colors.label); p.drawText(plot,Qt::AlignCenter,"No MIDI notes");}
}

void ScopeWindow::paintPattern(QPainter &p,int card,const QRectF &plot) {
    const auto &data=*state_.info.patterns;
    const auto pos=state_.pattern;
    const auto &colors=theme_.colors(sourceFor(card));
    if(pos.pattern<0||pos.pattern>=data.patterns.size()) return;
    const auto &pattern=data.patterns[pos.pattern];
    const bool mix=sourceFor(card)==ScopeTheme::FullMix, details=cards_[card].commands;
    const int first=mix?std::clamp(cards_[card].firstChannel,0,data.channels-1):sourceFor(card);
    auto font=QFontDatabase::systemFont(QFontDatabase::FixedFont); font.setPointSize(9); p.setFont(font);
    const QFontMetricsF metrics(font);
    const double rowHeight=std::ceil(metrics.height()+5), gutter=metrics.horizontalAdvance("000")+12;
    const double minimum=metrics.horizontalAdvance(details?"C-4 01 40 00:00 00:00":"C-4 01")+18;
    const int count=mix?std::min(data.channels-first,std::max(1,int((plot.width()-gutter)/minimum))):1;
    const double cellWidth=(plot.width()-gutter)/count;
    if(cellWidth<20||plot.height()<rowHeight*3) {p.setPen(colors.label);p.drawText(plot,Qt::AlignCenter,"Enlarge card");return;}
    p.setPen(colors.label);
    const QString status=QString("Order %1 · Pattern %2 · %3 BPM").arg(hex(pos.order)).arg(hex(pos.pattern)).arg(pos.bpm);
    p.drawText(QRectF(plot.left(),plot.top(),plot.width(),rowHeight),Qt::AlignVCenter,metrics.elidedText(status,Qt::ElideRight,int(plot.width())));
    if(mix&&count<data.channels) {
        p.drawText(QRectF(plot.left(),plot.top()+rowHeight,plot.width(),rowHeight),Qt::AlignVCenter,
            metrics.elidedText(QString("Channels %1–%2 / %3").arg(first+1).arg(first+count).arg(data.channels),Qt::ElideRight,int(plot.width())));
    }
    const double header=plot.top()+rowHeight*(mix&&count<data.channels?2:1);
    for(int c=0;c<count;++c) {
        const QRectF rect(plot.left()+gutter+c*cellWidth,header,cellWidth,rowHeight);
        p.drawText(rect,Qt::AlignCenter,metrics.elidedText(QString("Ch %1").arg(first+c+1),Qt::ElideRight,int(cellWidth)));
    }
    const double top=header+rowHeight;
    const int rows=std::max(1,int((plot.bottom()-top)/rowHeight));
    const int middle=rows/2;
    for(int i=0;i<rows;++i) {
        const int row=pos.row+i-middle;
        const QRectF area(plot.left(),top+i*rowHeight,plot.width(),rowHeight);
        if(row==pos.row) {auto ink=colors.waveform; ink.setAlpha(45); p.fillRect(area,ink);}
        if(row<0||row>=pattern.rows) continue;
        auto number=colors.label; number.setAlpha(row==pos.row?255:135); p.setPen(number);
        p.drawText(QRectF(area.left(),area.top(),gutter-5,rowHeight),Qt::AlignRight|Qt::AlignVCenter,hex(row));
        for(int c=0;c<count;++c) {
            const auto &cell=pattern.cells[row*data.channels+first+c];
            const QString instrument=cell.instrument?hex(cell.instrument):"..";
            QString text=trackerNoteName(cell.note)+" "+instrument;
            if(details) {
                const auto effect=[](int type,int param){return type||param?hex(type)+":"+hex(param):QString("..:..");};
                text+=" "+(cell.volume?hex(cell.volume-1):QString(".."))+" "+effect(cell.effect,cell.parameter)+" "+effect(cell.effect2,cell.parameter2);
            }
            auto ink=cell.note||cell.instrument?theme_.colors(cell.instrument?((cell.instrument-1)%ScopeTheme::SlotCount):first+c).waveform:colors.label;
            if(state_.muteMask.test(first+c)||!state_.outputMask) ink.setAlpha(85);
            else if(!cell.note&&!cell.instrument) ink.setAlpha(row==pos.row?210:100);
            p.setPen(ink);
            const QRectF rect(plot.left()+gutter+c*cellWidth+5,area.top(),cellWidth-10,rowHeight);
            p.drawText(rect,Qt::AlignLeft|Qt::AlignVCenter,metrics.elidedText(text,Qt::ElideRight,int(rect.width())));
        }
    }
    p.setPen(colors.axis);
    for(int c=0;c<=count;++c) {const double x=plot.left()+gutter+c*cellWidth;p.drawLine(QPointF(x,header),QPointF(x,plot.bottom()));}
}

QVector<ScopeOverlay> ScopeWindow::musicOverlays() {
    QVector<ScopeOverlay> result;
    QSet<int> keep;
    for(int id:displayedChannels()) {
        if(viewFor(id)<ViewKind::PianoRoll) continue;
        const auto plot=plotRect(id); if(plot.width()<2||plot.height()<2) continue;
        keep.insert(id);
        QByteArray key; QDataStream s(&key,QIODevice::WriteOnly);
        s<<plot.size()<<devicePixelRatioF()<<state_.generation<<state_.busy<<state_.muteMask<<state_.outputMask
         <<int(viewFor(id))<<cards_[id].horizontal<<cards_[id].commands<<cards_[id].firstChannel<<cards_[id].keyboardSize<<sourceFor(id);
        if(viewFor(id)==ViewKind::PianoRoll) s<<state_.positionMs;
        else s<<state_.pattern.order<<state_.pattern.pattern<<state_.pattern.row<<state_.pattern.bpm;
        const auto &colors=theme_.colors(sourceFor(id));
        s<<colors.background<<colors.axis<<colors.label<<colors.waveform;
        for(const auto &color:theme_.tiles) s<<color.waveform;
        auto &cache=musicImages_[id];
        if(cache.key!=key||cache.image.isNull()) {
            cache.key=key;
            cache.image=QImage(QSize(std::ceil(plot.width()*devicePixelRatioF()),std::ceil(plot.height()*devicePixelRatioF())),QImage::Format_ARGB32_Premultiplied);
            cache.image.setDevicePixelRatio(devicePixelRatioF()); cache.image.fill(Qt::transparent);
            QPainter p(&cache.image); p.setRenderHint(QPainter::Antialiasing);
            paintMusic(p,id,QRectF(QPointF(0,0),plot.size()));
        }
        if(!cache.image.isNull()) result.push_back({id,plot,cache.image});
    }
    for(auto it=musicImages_.begin();it!=musicImages_.end();) {if(!keep.contains(it.key())) it=musicImages_.erase(it);else ++it;}
    return result;
}

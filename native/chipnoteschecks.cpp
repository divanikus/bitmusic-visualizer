#include "window.h"
#include "spectrum.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
QByteArray gbReg(int address, int value) {
    QByteArray b; b += char(0x3e); b += char(value); b += char(0xe0); b += char(address); return b;
}
QByteArray gbs(int variant, bool changing) {
    QByteArray init;
    auto reg = [&](int a, int v) { init += gbReg(a, v); };
    reg(0x26, 0); reg(0x26, 0x80); reg(0x24, 0x77); reg(0x25, 0xff);
    reg(0x10, variant == 8 ? 0x11 : 0); reg(0x11, 0x80);
    reg(0x12, variant == 1 ? 0xf1 : 0xf0); reg(0x13, 0xd6); reg(0x14, 0x86);
    reg(0x16, variant == 2 ? 0x7f : 0x40); reg(0x17, 0xa0);
    reg(0x18, 0x83); reg(0x19, variant == 2 ? 0xc7 : 0x87);
    reg(0x1a, 0);
    for (int i = 0; i < 16; ++i) reg(0x30+i, variant == 6 ? 0 : i*17);
    reg(0x1a, 0x80); reg(0x1b, 0); reg(0x1c, 0x20); reg(0x1d, 0x40); reg(0x1e, 0x87);
    if (variant == 3) reg(0x1a, 0);
    if (variant == 4) reg(0x25, 0);
    if (variant == 5) reg(0x26, 0);
    if (variant == 7) for (auto a : {0x13, 0x18, 0x1d}) { reg(a, 255); reg(a+1, 0x87); }
    init += QByteArray::fromHex("af ea00c0 c9"); // Zero the play counter.
    QByteArray play(1, char(0xc9));
    if (changing) {
        const auto high = gbReg(0x13, 0x2a)+gbReg(0x14, 0x87)+char(0xc9);
        play = QByteArray::fromHex("fa00c0 3c ea00c0 e610 28")+char(high.size())+high+
            gbReg(0x13, 0xd6)+gbReg(0x14, 0x86)+char(0xc9);
    }
    QByteArray data(0x70, 0); data.replace(0, 6, QByteArray::fromHex("474253010101"));
    qToLittleEndian<quint16>(0x400, data.data()+6); qToLittleEndian<quint16>(0x400, data.data()+8);
    qToLittleEndian<quint16>(quint16(0x400+init.size()), data.data()+10);
    qToLittleEndian<quint16>(0xfffe, data.data()+12);
    return data+init+play;
}
QByteArray ayReg(int address, int value, bool cpc) {
    QByteArray b;
    auto out = [&](int port, int data) {
        b += char(0x01); b += char(port); b += char(port>>8);
        b += char(0x3e); b += char(data); b += QByteArray::fromHex("ed79");
    };
    if (cpc) { out(0xf400, address); out(0xf600, 0xc0); out(0xf400, value); out(0xf600, 0x80); }
    else { out(0xfffd, address); out(0xbffd, value); }
    return b;
}
QByteArray ay(int variant, bool changing, bool cpc) {
    QByteArray init;
    auto reg = [&](int a, int v) { init += ayReg(a, v, cpc); };
    reg(0, variant == 7 ? 0 : 180); reg(1, 0); reg(2, 70); reg(3, 1); reg(4, 150); reg(5, 2);
    reg(7, variant == 1 ? 0x39 : variant == 3 ? 7 : 0x38);
    reg(8, 15); reg(9, variant == 2 ? 0 : 12); reg(10, variant >= 4 && variant <= 6 ? 16 : 10);
    reg(11, variant == 6 ? 1 : 128); reg(12, 0); reg(13, variant == 4 ? 9 : variant == 5 ? 13 : 10);
    init += QByteArray::fromHex("af 320090 c9");
    QByteArray play(1, char(0xc9));
    if (changing) {
        const auto high = ayReg(0, 120, cpc)+char(0xc9);
        play = QByteArray::fromHex("3a0090 3c 320090 e610 28")+char(high.size())+high+
            ayReg(0, 180, cpc)+char(0xc9);
    }
    QByteArray data(0x80, 0); data.replace(0, 8, "ZXAYEMUL");
    auto word = [&](int a, int n) { qToBigEndian<quint16>(quint16(n), data.data()+a); };
    auto ptr = [&](int a, int n) { word(a, n-a); };
    ptr(18, 0x20); ptr(0x20, 0x70); ptr(0x22, 0x30); data.replace(0x70, 10, "Chip notes");
    ptr(0x3a, 0x50); ptr(0x3c, 0x60); word(0x50, 0xff00); word(0x52, 0x8000);
    word(0x54, 0x8000+init.size()); word(0x60, 0x8000); word(0x62, init.size()+play.size()); ptr(0x64, 0x80);
    return data+init+play;
}
QString write(const QDir& dir, const QString& name, const QByteArray& data) {
    QFile file(dir.filePath(name)); require(file.open(QIODevice::WriteOnly) && file.write(data)==data.size(), "Cannot write chip-note fixture.");
    return file.fileName();
}
void until(QApplication& app, const std::function<bool()>& ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 8000) { app.processEvents(); QThread::msleep(4); }
    require(ready(), "Chip-note UI did not reach the requested position.");
}
void live(QApplication& app, const QString& path, const QDir& dir, const QString& name) {
    PlayerWindow player; player.player().setVolume(0); player.show(); player.loadFile(path);
    auto& scope = player.scopeWindow(); scope.show();
    auto ready = [&] { const auto s=player.player().state(); const auto f=player.player().scopes();
        return !s.busy && s.generation==f.generation && f.mask==15; };
    until(app, ready); player.refresh(); scope.findChild<QPushButton*>("editLayout")->click();
    for (int ch = 0; ch < 4; ++ch) {
        scope.findChild<QListWidget*>("scopeChannelList")->setCurrentRow(ch);
        scope.findChild<QComboBox*>("cardView")->setCurrentIndex(2);
        require(scope.findChild<QLabel*>("keyboardHint")->text().contains("Estimated") == (ch == 3), "Chip/Estimated hint incorrect.");
    }
    scope.findChild<QPushButton*>("applyLayout")->click();
    player.player().setMuteMask(2);
    for (int ms : {12000, 200, 1000}) {
        player.player().seek(ms, false); until(app, ready);
        const auto frame=player.player().scopes();
        require(frame.positionMs==ms, "Chip notes use a stale seek position.");
        for (int ch=0; ch<3; ++ch) require(frame.noteHz.value(ch).value(0)>0, "Live chip notes unavailable.");
    }
    player.refresh(); scope.grab().save(dir.filePath(name+"-keyboard.png"));
    scope.hide(); app.processEvents(); scope.show(); player.player().seek(500, false); until(app, ready);
    require(player.player().scopes().noteHz.value(1).value(0)>0, "Muted/hidden chip voice lost notes.");
    player.close();
}
}

void checkGbAyNotes(QApplication& app, const QString& directory, QTextStream& log) {
    const QDir dir(directory);
    for (int system=0; system<3; ++system) {
        const bool gb=system==0, cpc=system==2;
        const QString name=gb ? "gbs" : cpc ? "cpc" : "ay";
        const QString ext=gb ? ".gbs" : ".ay";
        const float rate=gb ? 4194304.f : cpc ? 2000000.f : 3546900.f;
        const QVector<float> base=gb ? QVector<float>{rate/(32*(2048-0x6d6)),rate/(32*(2048-0x783)),rate/(64*(2048-0x740))}
            : QVector<float>{rate/(32*180),rate/(32*326),rate/(32*662)};
        for (int variant=0; variant<(gb ? 9 : 8); ++variant) {
            const auto path=write(dir, name+QString::number(variant)+ext, gb ? gbs(variant,false) : ay(variant,false,cpc));
            GmeTrack plain(path), captured(path,0,false,true);
            require(captured.info().tonalMask==7, "GBS/AY chip capability missing.");
            std::array<short, BlockFrames*2> a{}, b{};
            for (int i=0;i<44;++i) { plain.render(a.data(),512); captured.render(b.data(),512); require(a==b,"GBS/AY note capture changed audio."); }
            for (int ch=0;ch<3;++ch) {
                bool silent=gb ? (variant==1 && ch==0) || (variant==2 && ch==1) || ((variant==3 || variant==6) && ch==2) ||
                    variant==4 || variant==5 || variant==7 || (variant==8 && ch==0) :
                    (variant==1 && ch==0) || (variant==2 && ch==1) || variant==3 || (variant==4 && ch==2) || (variant==7 && ch==0);
                const float hz=captured.noteHz(ch);
                if (!gb && variant==6 && ch==2) require(hz==0 || std::abs(hz-base[ch])<.01, "Fast AY envelope invented pitch.");
                else require(std::abs(hz-(silent ? 0 : base[ch]))<.01, "GBS/AY oscillator frequency or gate incorrect.");
            }
            require(captured.noteHz(3)<0, "Noise/beeper incorrectly reported chip pitch.");
        }
        const auto path=write(dir,name+"-changing"+ext,gb ? gbs(0,true) : ay(0,true,cpc));
        GmeTrack track(path,0,false,true); track.mute(~1u);
        std::array<short, 2048*2> pcm{}; QVector<float> history;
        float previous=-1; int stable=0, compared=0; bool low=false, high=false;
        const float alternate=gb ? rate/(32*(2048-0x72a)) : rate/(32*120);
        for (int i=0;i<180*SampleRate/512;++i) {
            track.render(pcm.data(),512); const float hz=track.noteHz(0);
            require(hz>=0, "GBS/AY note clock has gaps.");
            low |= std::abs(hz-base[0])<.01; high |= std::abs(hz-alternate)<.01;
            stable=hz==previous ? stable+1 : 0; previous=hz;
            if (i<800) {
                for (int j=0;j<512;++j) history.push_back((pcm[j*2]+pcm[j*2+1])*.5f);
                if (history.size()>2048) history.remove(0,history.size()-2048);
                if (stable>=5 && history.size()==2048 && hz>0) {
                    const auto bins=scopeSpectrum(history); const int peak=int(std::max_element(bins.begin(),bins.end())-bins.begin());
                    require(std::abs(peak*SampleRate/2048.-hz)<22, "GBS/AY pitch disagrees with delivered PCM."); ++compared;
                }
            }
        }
        require(low && high && compared>100, "Changing chip-note fixture not exercised.");
        for (qint64 at : {SampleRate*20LL, SampleRate/3LL, SampleRate*60LL}) {
            track.seek(at); track.render(pcm.data(),128); require(track.noteHz(0)>0,"GBS/AY notes lost after seek.");
        }
        const auto steady=dir.filePath(name+"0"+ext);
        for (int ch=0;ch<3;++ch) {
            GmeTrack solo(steady,0,false,true); solo.mute(~(1u<<ch)); solo.seek(SampleRate/4);
            QVector<float> samples(2048); solo.render(pcm.data(),2048);
            for(int i=0;i<2048;++i) samples[i]=(pcm[i*2]+pcm[i*2+1])*.5f;
            const auto bins=scopeSpectrum(samples); const int peak=int(std::max_element(bins.begin(),bins.end())-bins.begin());
            require(std::abs(peak*SampleRate/2048.-solo.noteHz(ch))<22,"GBS/AY solo PCM disagrees with chip note.");
        }
        live(app,steady,dir,name);
        log << name << ": chip frequencies/gates, PCM equality/alignment, 180-second continuity, seeks and live Keyboard PASS\n"; log.flush();
    }
}

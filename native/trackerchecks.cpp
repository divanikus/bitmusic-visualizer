#include "trackertrack.h"
#include "playerwindow.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool v, const char *s) { if (!v) throw std::runtime_error(s); }
QByteArray pcm(MusicTrack &t, int frames) {
    QByteArray b(frames*4, 0); t.render(reinterpret_cast<short *>(b.data()), frames); return b;
}
float peak(const QVector<float> &a) { float p=0; for (auto x:a) p=std::max(p,std::abs(x)); return p; }
void spin(QApplication &app, const std::function<bool()> &ready, int ms=6000) {
    QElapsedTimer time; time.start();
    while (!ready() && time.elapsed()<ms) { app.processEvents(); QThread::msleep(5); }
    require(ready(), "Tracker live state timed out.");
}
}
int runTrackerChecks(QApplication &app, const QStringList &args) {
    QTextStream log(stdout);
    try {
        const int at=args.indexOf("--tracker-checks");
        require(at>=0 && at+1<args.size(), "Supply tracker fixture directory.");
        QDir dir{QDir{args[at+1]}.absolutePath()};
        QTemporaryDir settings(dir.filePath("tracker-settings-XXXXXX"));
        require(settings.isValid(), "Cannot create tracker test settings.");
        app.setProperty("settingsFileForTests", settings.filePath("settings.ini"));
        app.setProperty("themesDirectoryForTests", settings.filePath("themes"));
        app.setProperty("presetsDirectoryForTests", settings.filePath("presets"));
        const auto path=dir.filePath("demo.mod");
        auto plain=openMusicTrack(path), tapped=openMusicTrack(path,0,true,true);
        const auto &info=tapped->info();
        require(info.voices.size()==4 && info.stereoOutput && info.system=="Amiga" &&
            info.title=="Bit Music MOD test" && info.configuration.name.contains("Paula"),
            "MOD metadata or Paula routing failed.");
        require(info.durationMs==7680 && info.songs==1, "MOD duration or sequences failed.");
        for (int n : {1, 257, 1024, 3000, 4096, 137})
            require(pcm(*plain,n)==pcm(*tapped,n), "Capturing tracker voices changed audio.");
        require(tapped->instrumentNames(0).value(0)=="Sine", "Tracker sample names missing.");
        QVector<QVector<float>> left(4), right(4);
        for (int c=0; c<4; ++c) {
            require(tapped->readTap(c,left[c],right[c]), "Missing tracker tap.");
            const bool l=c==0 || c==3;
            log << "Channel " << c << " L/R peaks " << peak(left[c]) << '/' << peak(right[c]) << '\n'; log.flush();
            // libxmp's 0..255 pan retains 1/256 on the opposite side at far right.
            require(peak(l?left[c]:right[c])>100 && peak(l?right[c]:left[c])<peak(l?left[c]:right[c])*.005f, "Amiga LRRL pan or distinct voices failed.");
        }
        require(left[0]!=left[3] && right[1]!=right[2], "Tracker voices were duplicated.");
        // Sum of independently captured channels must reconstruct the unclipped master.
        const auto from=tapped->frame();
        const auto block=pcm(*tapped,ScopeFrames);
        for (int c=0;c<4;++c) tapped->readTap(c,left[c],right[c]);
        const auto *samples=reinterpret_cast<const short*>(block.constData());
        for (int i=0;i<ScopeFrames;++i) for (int side=0;side<2;++side) {
            float sum=0; for(int c=0;c<4;++c) sum+=(side?right[c]:left[c])[i];
            require(std::abs(sum-samples[i*2+side])<2, "Tracker taps do not reconstruct master PCM.");
        }
        require(tapped->seek(from), "Tracker rewind failed.");
        require(pcm(*tapped,ScopeFrames)==block, "Tracker rewind is not deterministic.");
        auto other=openMusicTrack(path,0,false,true);
        other->seek(from); require(pcm(*other,ScopeFrames)==block, "Independent tracker decoders drifted.");
        require(!other->seek(0,[]{return true;}) && other->frame()==from+ScopeFrames, "Cancelled tracker seek changed state.");
        for(int c=0;c<4;++c) {
            auto solo=openMusicTrack(path);
            solo->mute(~VoiceMask::bit(c)); solo->seek(from);
            const auto audio=pcm(*solo,ScopeFrames);
            const auto *v=reinterpret_cast<const short*>(audio.constData());
            for(int i=0;i<ScopeFrames;++i) for(int side=0;side<2;++side)
                require(std::abs((side?right[c]:left[c])[i]-v[2*i+side])<2, "Tracker Solo leaked or changed another voice.");
        }
        auto muted=openMusicTrack(path);
        muted->mute(VoiceMask::first(4)); muted->seek(from);
        const auto silence=pcm(*muted,ScopeFrames);
        require(std::all_of(silence.begin(),silence.end(),[](char b){return b==0;}), "Tracker mute leaked.");
        auto old=openMusicTrack(dir.filePath("soundtracker.mod"));
        require(old->info().voices.size()==4 && !pcm(*old,4096).isEmpty(), "15-sample Soundtracker failed.");
        auto eight=openMusicTrack(dir.filePath("multichannel.mod"),0,true,true);
        require(eight->info().voices.size()==8, "Multichannel MOD failed.");
        pcm(*eight,4096); QVector<float> a,b;
        require(eight->readTap(7,a,b) && std::max(peak(a),peak(b))>100, "High tracker channel missing.");
        auto seq=openMusicTrack(dir.filePath("sequences.mod"));
        require(seq->info().songs==2 && seq->info().playlist.size()==2, "Tracker subsongs missing.");
        auto second=openMusicTrack(dir.filePath("sequences.mod"),1);
        require(second->info().song==1 && pcm(*second,4096)==pcm(*seq,4096), "Tracker sequence selection failed.");
        require(!isTrackerModule(QByteArray("not music")) && !isTrackerModule(QByteArray(100,0)), "Invalid tracker input accepted.");
        log << "Tracker decoder PASS: MOD/15-sample/8-channel, subsongs, duration, exact capture/mix/Solo, stereo, names, rewind and cancellation.\n"; log.flush();
        // Optional local modules are never copied into the source tree or packages.
        auto extra=args.mid(at+2); extra.removeAll("--decoder-only");
        for (const auto &file : extra) {
            auto audio=openMusicTrack(file), scope=openMusicTrack(file,0,false,true);
            require(audio->info().configuration.family=="tracker", "Extra fixture is not a tracker module.");
            QElapsedTimer timer; timer.start();
            double energy=0;
            for (int i=0;i<200;++i) {
                const auto bytes=pcm(*audio,1024);
                require(bytes==pcm(*scope,1024), "Capture changed PCM in an extra tracker format.");
                const auto *v=reinterpret_cast<const short*>(bytes.constData());
                for(int j=0;j<2048;++j) energy+=std::abs(int(v[j]));
            }
            require(energy>100, "Extra tracker fixture is silent.");
            const auto summed=pcm(*scope,ScopeFrames);
            QVector<float> sumL(ScopeFrames),sumR(ScopeFrames);
            for(int c=0;c<scope->info().voices.size();++c) {
                require(scope->readTap(c,a,b), "Extra tracker source tap unavailable.");
                for(int i=0;i<ScopeFrames;++i) { sumL[i]+=a[i]; sumR[i]+=b[i]; }
            }
            double error=0;
            const auto *v=reinterpret_cast<const short*>(summed.constData());
            for(int i=0;i<ScopeFrames;++i) {
                error+=std::abs(std::clamp(sumL[i],-32768.f,32767.f)-v[i*2]);
                error+=std::abs(std::clamp(sumR[i],-32768.f,32767.f)-v[i*2+1]);
            }
            require(error/(ScopeFrames*2)<2, "Extra tracker taps do not reconstruct their mix.");
            require(scope->seek(0), "Extra tracker rewind failed.");
            auto fresh=openMusicTrack(file);
            require(pcm(*scope,4096)==pcm(*fresh,4096), "Extra tracker rewind changed PCM.");
            log << "Extra module PASS: " << QFileInfo(file).fileName() << " / "
                << audio->info().configuration.name << " / 4.6s dual decode in " << timer.elapsed() << "ms\n"; log.flush();
        }
        if (!args.contains("--decoder-only")) {
            PlayerWindow window; window.player().setVolume(0); window.show(); window.scopeWindow().show();
            window.loadFile(path);
            auto &player=window.player();
            player.setScopesEnabled(true); player.setScopeMask(VoiceMask::first(4));
            spin(app,[&]{return player.state().playing && player.scopes().mask==VoiceMask::first(4);});
            player.toggleSolo(2);
            require(player.state().soloChannel==2, "Live tracker Solo failed.");
            player.toggleSolo(2);
            player.seek(3500,true);
            spin(app,[&]{auto s=player.state(); auto f=player.scopes(); return !s.busy && s.positionMs>=3500 && f.generation==s.generation && f.mask==VoiceMask::first(4);});
            player.setRepeat(true); player.seek(7400,true);
            spin(app,[&]{return !player.state().busy && player.state().positionMs>=7400;});
            spin(app,[&]{return !player.state().busy && player.state().positionMs<2000 && player.scopes().mask==VoiceMask::first(4);});
            player.setRepeat(false); player.seek(7400,true);
            spin(app,[&]{auto s=player.state(); return !s.busy && !s.playing && s.positionMs>=s.info.durationMs;});
            player.seek(0,true);
            spin(app,[&]{auto s=player.state(); return !s.busy && s.playing && s.positionMs<2000 && player.scopes().generation==s.generation;});
            window.scopeWindow().grab().save(dir.filePath("tracker-scopes.png"));
            window.close();
            log << "Tracker live player PASS: audio, scopes, seek, repeat, end/restart and Solo.\n";
        }
        return 0;
    } catch (const std::exception &e) { log << "FAIL: " << e.what() << '\n'; return 1; }
}

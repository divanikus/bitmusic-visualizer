#pragma once
#include "appsettings.h"
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QSysInfo>
#include <cstdio>

// Opt-in, bounded local diagnostics. No music data, telemetry or automatic upload.
class DiagnosticLog {
public:
    explicit DiagnosticLog(bool enabled) {
        if (!enabled) return;
#ifdef Q_OS_WIN
        const auto directory = QCoreApplication::applicationDirPath();
#else
        const auto directory = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath("BitMusicVisualizer");
#endif
        path_ = QDir(directory).filePath("player-diagnostics.log");
        if (!QDir().mkpath(directory)) return;
        if (QFile::exists(path_)) {
            QFile::remove(path_ + ".previous");
            if (!QFile::rename(path_, path_ + ".previous")) return;
        }
        file_.setFileName(path_);
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        target_ = &file_; clock_.start(); previous_ = qInstallMessageHandler(message);
        qputenv("QSG_INFO", "1");
        QLoggingCategory::setFilterRules("qt.rhi.general=true\nqt.scenegraph.general=true");
        qInfo().noquote() << "Diagnostics" << QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
            << "app=" << QCoreApplication::applicationVersion() << "Qt=" << qVersion()
            << "OS=" << QSysInfo::prettyProductName() << "arch=" << QSysInfo::currentCpuArchitecture()
            << "requestedGPU=" << qEnvironmentVariable("QSG_RHI_BACKEND")
            << "softwareScopes=" << qEnvironmentVariableIntValue("BITMUSIC_SOFTWARE_SCOPES");
    }
    ~DiagnosticLog() {
        if (file_.isOpen()) { qInstallMessageHandler(previous_); QMutexLocker lock(&mutex_); target_ = nullptr; }
    }
    bool active() const { return file_.isOpen(); }
    QString path() const { return path_; }
private:
    static void message(QtMsgType type, const QMessageLogContext &context, const QString &text) {
        QMutexLocker lock(&mutex_);
        if (!target_ || (target_->size() >= 2*1024*1024 && type != QtFatalMsg)) return;
        const auto line = QString("%1ms [%2] %3: %4\n").arg(clock_.elapsed()).arg(int(type))
            .arg(QString::fromUtf8(context.category ? context.category : "default"), text.left(16000)).toUtf8();
        target_->write(line); target_->flush();
    }
    QString path_;
    QFile file_;
    inline static QFile *target_ = nullptr;
    inline static QMutex mutex_;
    inline static QElapsedTimer clock_;
    QtMessageHandler previous_ = nullptr;
};

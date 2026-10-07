#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

inline QString playerSettingsPath() {
    // Tests always use an isolated INI, independently of the host platform.
    const auto testPath = QCoreApplication::instance()->property("settingsFileForTests").toString();
    if (!testPath.isEmpty()) return testPath;
#if defined(Q_OS_WIN)
    return QDir(QCoreApplication::applicationDirPath()).filePath("BitMusicVisualizer.ini");
#elif defined(Q_OS_MACOS)
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath("org.bitmusicvisualizer.BitMusicVisualizer.plist");
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath("BitMusicVisualizer/BitMusicVisualizer.ini");
#endif
}

inline QSettings::Format playerSettingsFormat() {
#if defined(Q_OS_MACOS)
    if (QCoreApplication::instance()->property("settingsFileForTests").toString().isEmpty())
        return QSettings::NativeFormat;
#endif
    return QSettings::IniFormat;
}

inline QString playerThemesDirectory() {
    const auto testPath = QCoreApplication::instance()->property("themesDirectoryForTests").toString();
    if (!testPath.isEmpty()) return testPath;
#if defined(Q_OS_WIN)
    return QDir(QCoreApplication::applicationDirPath()).filePath("themes");
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath("BitMusicVisualizer/themes");
#endif
}

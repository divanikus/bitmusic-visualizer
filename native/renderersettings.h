#pragma once
#include "appsettings.h"

inline QString scopeRendererPreference() {
    QSettings settings(playerSettingsPath(), playerSettingsFormat());
    settings.setFallbacksEnabled(false);
    const auto value = settings.value("Renderer/Backend", "auto").toString();
    if (value == "opengl" || value == "cpu") return value;
#ifdef Q_OS_WIN
    if (value == "d3d11") return value;
#endif
    return "auto";
}

// Call before constructing any Quick window. Explicit diagnostic/environment
// overrides take precedence over the saved choice, without changing preferences.
inline void configureScopeRenderer() {
    if (qEnvironmentVariableIsSet("BITMUSIC_SOFTWARE_SCOPES") ||
        qEnvironmentVariableIsSet("QSG_RHI_BACKEND") || qEnvironmentVariableIsSet("QT_QUICK_BACKEND")) return;
    const auto value = scopeRendererPreference();
    if (value == "cpu") qputenv("BITMUSIC_SOFTWARE_SCOPES", "1");
    else if (value != "auto") qputenv("QSG_RHI_BACKEND", value.toLatin1());
}

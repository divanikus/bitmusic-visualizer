#include "resourceerror.h"
#include <array>

namespace {
struct ResourceText { const char *prefix, *title; };
constexpr std::array<ResourceText, 4> messages{{
    {"C64 ROM required:", "ROM required"},
    {"YM2608 ROM required:", "ROM required"},
    {"SoundFont required:", "SoundFont required"},
    {"MT-32 ROM required:", "ROM required"}
}};
}
QString resourceRequiredMessage(RequiredResource resource, const QString &detail) {
    return QString::fromLatin1(messages.at(size_t(resource)).prefix) + ' ' + detail;
}
QString resourceWarningTitle(const QString &error) {
    for (const auto &message : messages)
        if (error.startsWith(QLatin1String(message.prefix))) return QString::fromLatin1(message.title);
    return {};
}

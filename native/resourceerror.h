#pragma once
#include <QString>

enum class RequiredResource { C64Rom, Ym2608Rom, SoundFont, Mt32Rom };

// Stable text boundary between decoder errors and the player warning policy.
QString resourceRequiredMessage(RequiredResource resource, const QString &detail);
// Empty for ordinary load/validation errors. Matching is case-sensitive at the start.
QString resourceWarningTitle(const QString &error);

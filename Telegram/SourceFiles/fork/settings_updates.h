/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

namespace Fork::Updates {

inline constexpr auto kShowBetaOptions = false;
inline constexpr auto kShowInAdvancedSettings = false;

void PrepareStartupCheck();
[[nodiscard]] ::Settings::Type SectionId();

} // namespace Fork::Updates

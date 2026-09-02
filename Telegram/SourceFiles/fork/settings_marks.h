/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

// The messages page: how kept and edited messages are marked, and whether
// deleted ones are painted translucent. Opened from the fork's main page,
// see fork/settings_seegram.h.

namespace Fork::Marks {

[[nodiscard]] ::Settings::Type SectionId();

} // namespace Fork::Marks

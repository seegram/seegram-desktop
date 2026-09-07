/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

// The see.tg integration page: the switch, and a way to sign in again.
// Opened from the fork's main page, see fork/settings_seegram.h.

namespace Fork::SeeTg {

[[nodiscard]] ::Settings::Type SectionId();

} // namespace Fork::SeeTg

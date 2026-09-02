/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

// The spy mode page: which of the things the server takes back are kept.
// Opened from the fork's main page, see fork/settings_seegram.h.

namespace Fork::Spy {

[[nodiscard]] ::Settings::Type SectionId();

} // namespace Fork::Spy

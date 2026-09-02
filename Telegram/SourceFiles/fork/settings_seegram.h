/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

// The fork's own page in Settings, laid out the way AyuGram lays out its
// preferences: a title, the version, and a list of categories that each
// open a page of their own - ghost mode, spy mode, message marks. One flat
// page held four switches well enough; it would not hold what the spy
// features add without turning into a heap.
//
// Lives here rather than in upstream's settings files, so that those keep
// the one-line call and nothing more. See fork/RULES.md.

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Fork::SeeGram {

[[nodiscard]] ::Settings::Type SectionId();

// Hook, called from Settings' own section list.
void AddSettingsButton(::Settings::Builder::SectionBuilder &builder);

} // namespace Fork::SeeGram

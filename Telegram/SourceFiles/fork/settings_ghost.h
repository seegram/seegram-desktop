/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

// The two places ghost mode is operated from, laid out the way AyuGram lays
// them out: a page of its own for the four switches, opened from the fork's
// main page in Settings (see fork/settings_seegram.h), and a single switch
// at the foot of the side menu for turning the lot on and off without
// opening anything.
//
// Both live here rather than in upstream's own settings and menu files, so
// that those keep the one-line call each and nothing more. See fork/RULES.md.

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Fork::Ghost {

[[nodiscard]] ::Settings::Type SectionId();

// Hook, called from the bottom of the side menu.
void SetupMainMenuToggle(not_null<Ui::VerticalLayout*> container);

} // namespace Fork::Ghost

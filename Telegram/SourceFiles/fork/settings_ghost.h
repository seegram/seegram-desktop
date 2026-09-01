/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

// The two places ghost mode is operated from, laid out the way AyuGram lays
// them out: a section of its own in Settings for the four switches, and a
// single switch at the foot of the side menu for turning the lot on and off
// without opening anything.
//
// Both live here rather than in upstream's own settings and menu files, so
// that those keep the one-line call each and nothing more.

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Fork::Ghost {

[[nodiscard]] ::Settings::Type SectionId();

// Hook, called from Settings' own section list.
void AddSettingsButton(::Settings::Builder::SectionBuilder &builder);

// Hook, called from the bottom of the side menu.
void SetupMainMenuToggle(not_null<Ui::VerticalLayout*> container);

} // namespace Fork::Ghost

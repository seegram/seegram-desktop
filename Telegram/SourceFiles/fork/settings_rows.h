#pragma once

#include "settings/detailed_settings_button.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_settings.h"

namespace Fork::SettingsRows {

inline not_null<DetailedSettingsButton*> AddToggle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title,
		rpl::producer<QString> description,
		rpl::producer<bool> value) {
	// Keep the style alive with the section and use Telegram's native row.
	const auto style = container->lifetime().make_state<style::DetailedSettingsButtonStyle>(
		st::detailedSettingsButtonStyle);
	style->button.padding = st::settingsButtonNoIcon.padding;
	style->description = st::settingsExperimentalAbout;
	return AddDetailedSettingsButton(container, std::move(title),
		std::move(description), {}, std::move(value), *style);
}

inline void AddDescription(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	container->add(object_ptr<Ui::FlatLabel>(container, std::move(text),
		st::settingsExperimentalAbout),
		QMargins(st::settingsButtonNoIcon.padding.left(), 0,
			st::settingsButtonNoIcon.padding.right(),
			st::settingsExperimentalAboutPadding.bottom()));
}

} // namespace Fork::SettingsRows

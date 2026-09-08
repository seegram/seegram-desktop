/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/settings_spy.h"

#include "fork/fork_lang.h"
#include "fork/settings_rows.h"
#include "fork/spy_mode.h"
#include "settings/settings_common_session.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace Fork::Spy {
namespace {

// The strings are in fork/fork_lang.cpp.
using Lang::Key;

void AddSwitch(
		not_null<Ui::VerticalLayout*> container,
		Key text,
		bool Settings::*field,
		std::optional<Key> description = std::nullopt) {
	const auto value = Value() | rpl::map([=](const Settings &settings) {
		return settings.*field;
	});
	const auto bind = [=](auto button) {
		button->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != Current().*field);
		}) | rpl::on_next([=](bool toggled) {
			auto settings = Current();
			settings.*field = toggled;
			Set(settings);
		}, button->lifetime());
	};
	if (description) {
		bind(SettingsRows::AddToggle(container, Lang::Value(text),
			Lang::Value(*description), rpl::duplicate(value)));
	} else {
		const auto button = container->add(object_ptr<Ui::SettingsButton>(
			container, Lang::Value(text), st::settingsButtonNoIcon));
		button->toggleOn(rpl::duplicate(value));
		bind(button);
	}
}

void BuildContent(not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, Lang::Value(Key::SpyEssentials));
	AddSwitch(
		container,
		Key::SaveDeletedMessages,
		&Settings::saveDeletedMessages);
	AddSwitch(container, Key::SaveEditsHistory, &Settings::saveEditsHistory);
	Ui::AddSkip(container);
	SettingsRows::AddDescription(container, Lang::Value(Key::SpyAboutSaving));

	Ui::AddSkip(container);
	AddSwitch(container, Key::SaveForBots, &Settings::saveForBots, Key::SpyAboutBots);
	Ui::AddSkip(container);
	AddSwitch(container, Key::PreviewSelfDestructMedia, &Settings::previewSelfDestructMedia, Key::SelfDestructMediaAbout);
}

class SpySection final : public ::Settings::Section<SpySection> {
public:
	SpySection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

SpySection::SpySection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	BuildContent(content);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> SpySection::title() {
	return Lang::Value(Key::SpyMode);
}

} // namespace

::Settings::Type SectionId() {
	return SpySection::Id();
}

} // namespace Fork::Spy

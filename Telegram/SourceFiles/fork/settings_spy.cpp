/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/settings_spy.h"

#include "fork/fork_lang.h"
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
		bool Settings::*field) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		Lang::Value(text),
		st::settingsButtonNoIcon));
	button->toggleOn(Value() | rpl::map([=](const Settings &settings) {
		return settings.*field;
	}));
	button->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return (toggled != Current().*field);
	}) | rpl::on_next([=](bool toggled) {
		auto settings = Current();
		settings.*field = toggled;
		Set(settings);
	}, button->lifetime());
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
	Ui::AddDividerText(container, Lang::Value(Key::SpyAboutSaving));

	Ui::AddSkip(container);
	AddSwitch(container, Key::SaveForBots, &Settings::saveForBots);
	Ui::AddSkip(container);
	Ui::AddDividerText(container, Lang::Value(Key::SpyAboutBots));
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

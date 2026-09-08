/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/settings_updates.h"

#include "fork/about_seegram.h"
#include "fork/fork_lang.h"
#include "fork/settings_rows.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/click_handler_types.h"
#include "core/update_checker.h"
#include "settings/settings_common_session.h"
#include "settings/sections/settings_advanced.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Fork::Updates {
namespace {

constexpr auto kStartupKey = "seegram.checkUpdatesOnStartup";
auto StartupHandled = false;

[[nodiscard]] bool CheckOnStartup() {
	return Core::App().settings().readPref<bool>(kStartupKey, true);
}

class Section final : public ::Settings::Section<Section> {
public:
	Section(QWidget *parent, not_null<Window::SessionController*> controller);
	[[nodiscard]] rpl::producer<QString> title() override;
};

Section::Section(QWidget *parent, not_null<Window::SessionController*> controller)
: Settings::Section<Section>(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"SeeGram "_q + About::VersionText()));
	::Settings::SetupUpdate(content);
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	if (::Settings::HasUpdate()) {
		const auto startup = SettingsRows::AddToggle(
			content, Lang::Value(Lang::Key::UpdatesOnStartup),
			Lang::Value(Lang::Key::UpdatesStartupAbout),
			rpl::single(CheckOnStartup()));
		startup->toggledChanges() | rpl::on_next([](bool enabled) {
			Core::App().settings().writePref<bool>(kStartupKey, enabled);
			Core::App().saveSettingsDelayed();
		}, startup->lifetime());
		Ui::AddSkip(content);
		Ui::AddSkip(content);
	}
	const auto releases = content->add(object_ptr<Ui::SettingsButton>(
		content, Lang::Value(Lang::Key::AboutReleases), st::settingsButtonNoIcon));
	releases->addClickHandler([] {
		UrlClickHandler::Open(u"https://github.com/seegram/seegram-desktop/releases"_q);
	});
	Ui::AddSkip(content);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> Section::title() {
	return Lang::Value(Lang::Key::UpdatesTitle);
}

} // namespace

void PrepareStartupCheck() {
	if (StartupHandled) {
		return;
	}
	StartupHandled = true;
	if (cAutoUpdate() && CheckOnStartup()) {
		cSetLastUpdateCheck(0);
	}
}

::Settings::Type SectionId() {
	return Section::Id();
}

} // namespace Fork::Updates

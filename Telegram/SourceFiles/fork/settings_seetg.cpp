/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/settings_seetg.h"

#include "fork/fork_lang.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_auth.h"
#include "fork/seetg/seetg_settings.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace Fork::SeeTg {
namespace {

using Lang::Key;

void BuildContent(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, Lang::Value(Key::SeeTgTitle));

	const auto toggle = container->add(object_ptr<Ui::SettingsButton>(
		container,
		Lang::Value(Key::SeeTgEnabled),
		st::settingsButtonNoIcon));
	toggle->toggleOn(EnabledValue());
	toggle->toggledChanges(
	) | rpl::filter([](bool toggled) {
		return (toggled != Enabled());
	}) | rpl::on_next([](bool toggled) {
		auto settings = Current();
		settings.enabled = toggled;
		Set(settings);
	}, toggle->lifetime());

	const auto again = container->add(object_ptr<Ui::SettingsButton>(
		container,
		Lang::Value(Key::SeeTgSignInAgain),
		st::settingsButtonNoIcon));
	again->addClickHandler([=] {
		const auto session = &controller->session();
		Auth::Invalidate(session);
		Api::ClearCache(session);
		controller->showToast(Lang::Text(Key::SeeTgSignedOut));
	});

	Ui::AddSkip(container);
	Ui::AddDividerText(container, Lang::Value(Key::SeeTgAbout));

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, Lang::Value(Key::SeeTgResolveTitle));
	auto modeLabel = Value() | rpl::map([](const Settings &settings) {
		return (settings.resolve == ResolveMode::ByUsername)
			? Lang::Text(Key::SeeTgResolveByUsername)
			: Lang::Text(Key::SeeTgResolveByGift);
	});
	::Settings::AddButtonWithLabel(
		container,
		Lang::Value(Key::SeeTgResolveTitle),
		std::move(modeLabel),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto options = std::vector<QString>{
				Lang::Text(Key::SeeTgResolveByGift),
				Lang::Text(Key::SeeTgResolveByUsername),
			};
			SingleChoiceBox(box, {
				.title = Lang::Value(Key::SeeTgResolveTitle),
				.options = options,
				.initialSelection = (Current().resolve == ResolveMode::ByGift)
					? 0
					: 1,
				.callback = [](int index) {
					auto settings = Current();
					settings.resolve = index
						? ResolveMode::ByUsername
						: ResolveMode::ByGift;
					Set(settings);
				},
			});
		}));
	});

	const auto various = container->add(object_ptr<Ui::SettingsButton>(
		container,
		Lang::Value(Key::SeeTgResolveAuto),
		st::settingsButtonNoIcon));
	various->toggleOn(Value() | rpl::map([](const Settings &settings) {
		return settings.resolveAutomatically;
	}));
	various->toggledChanges(
	) | rpl::filter([](bool toggled) {
		return (toggled != Current().resolveAutomatically);
	}) | rpl::on_next([](bool toggled) {
		auto settings = Current();
		settings.resolveAutomatically = toggled;
		Set(settings);
	}, various->lifetime());

	const auto fallbackWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto fallback = fallbackWrap->entity()->add(
		object_ptr<Ui::SettingsButton>(
			fallbackWrap->entity(),
			Lang::Value(Key::SeeTgResolveFallback),
			st::settingsButtonNoIcon));
	fallback->toggleOn(Value() | rpl::map([](const Settings &settings) {
		return settings.usernameFallback;
	}));
	fallback->toggledChanges(
	) | rpl::filter([](bool toggled) {
		return (toggled != Current().usernameFallback);
	}) | rpl::on_next([](bool toggled) {
		auto settings = Current();
		settings.usernameFallback = toggled;
		Set(settings);
	}, fallback->lifetime());
	fallbackWrap->toggleOn(Value() | rpl::map([](const Settings &settings) {
		return (settings.resolve == ResolveMode::ByGift);
	}));

	Ui::AddSkip(container);
	Ui::AddDividerText(container, Lang::Value(Key::SeeTgResolveAbout));
}

class SeeTgSection final : public ::Settings::Section<SeeTgSection> {
public:
	SeeTgSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

SeeTgSection::SeeTgSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	BuildContent(content, controller);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> SeeTgSection::title() {
	return Lang::Value(Key::SeeTgTitle);
}

} // namespace

::Settings::Type SectionId() {
	return SeeTgSection::Id();
}

} // namespace Fork::SeeTg

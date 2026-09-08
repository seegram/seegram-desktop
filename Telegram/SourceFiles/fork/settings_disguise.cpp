#include "fork/settings_disguise.h"

#include "core/application.h"
#include "fork/disguise.h"
#include "fork/fork_lang.h"
#include "fork/settings_rows.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "settings/settings_common_session.h"
#include "storage/storage_domain.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"

#include "styles/style_about_seegram.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Fork::Disguise {
namespace {

using Lang::Key;

Storage::Domain &Store() {
	return Core::App().domain().local();
}

void EditName(not_null<Ui::GenericBox*> box) {
	box->setTitle(Lang::Value(Key::DisguiseName));
	const auto field = box->addRow(object_ptr<Ui::InputField>(box,
		st::defaultInputField,
		rpl::single(u"SeeGram"_q),
		Store().disguiseSettings().name));
	box->addRow(object_ptr<Ui::FlatLabel>(box,
		Lang::Value(Key::DisguiseNameAbout), st::boxDividerLabel));
	const auto save = [=] {
		auto settings = Store().disguiseSettings();
		settings.name = field->getLastText().trimmed();
		if (!Store().setDisguiseSettings(settings)) {
			field->showError();
			return;
		}
		box->closeBox();
	};
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] { field->setFocusFast(); });
}

class DisguiseSection final : public ::Settings::Section<DisguiseSection> {
public:
	DisguiseSection(QWidget *parent,
		not_null<Window::SessionController*> controller);
	rpl::producer<QString> title() override;

};

DisguiseSection::DisguiseSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	if (Store().restrictedProfile()) {
		return;
	}
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	Ui::AddSkip(content);
	const auto preview = content->add(object_ptr<Ui::RpWidget>(content),
		st::defaultBoxDividerLabelPadding);
	preview->resize(preview->width(), st::seegramAboutLogo);
	const auto name = Ui::CreateChild<Ui::FlatLabel>(preview,
		NameValue(), st::boxTitle);
	const auto placeName = [=] {
		const auto left = st::seegramAboutLogo + st::seegramAboutGap;
		name->resizeToWidth(std::max(1, preview->width() - left));
		preview->resize(preview->width(), std::max(st::seegramAboutLogo, name->height()));
		name->moveToLeft(left, (preview->height() - name->height()) / 2);
	};
	preview->widthValue() | rpl::on_next(placeName, preview->lifetime());
	Changes() | rpl::on_next([=] {
		placeName();
		preview->update();
	}, preview->lifetime());
	preview->paintRequest() | rpl::on_next([=] {
		auto p = Painter(preview);
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(QRect(QPoint(), QSize(st::seegramAboutLogo,
			st::seegramAboutLogo)), Image());
	}, preview->lifetime());
	Ui::AddSkip(content);
	SettingsRows::AddDescription(content, Lang::Value(Key::DisguiseAbout));
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	::Settings::AddButtonWithLabel(content,
		Lang::Value(Key::DisguiseName), NameValue(), st::settingsButton,
		{ &st::menuIconEdit })->addClickHandler([=] {
		controller->show(Box(EditName));
	});
	const auto iconValue = rpl::single(rpl::empty) | rpl::then(Changes())
		| rpl::map([] {
			return Store().disguiseSettings().icon == Icon::Telegram
				? u"Telegram"_q : u"SeeGram"_q;
		});
	::Settings::AddButtonWithLabel(content,
		Lang::Value(Key::DisguiseIcon), iconValue, st::settingsButton,
		{ &st::menuIconPhoto })->addClickHandler([=] {
		controller->show(Box([](not_null<Ui::GenericBox*> box) {
			SingleChoiceBox(box, {
				.title = Lang::Value(Key::DisguiseIcon),
				.options = { u"SeeGram"_q, u"Telegram"_q },
				.initialSelection = int(Store().disguiseSettings().icon),
				.callback = [](int index) {
					auto settings = Store().disguiseSettings();
					settings.icon = Icon(index);
					if (!Store().setDisguiseSettings(settings)) {
						return;
					}
				},
			});
		}));
	});
	Ui::AddSkip(content);
	const auto trayValue = rpl::single(rpl::empty) | rpl::then(Changes())
		| rpl::map([] {
			const auto icon = Store().disguiseSettings().trayIcon;
			return icon == TrayIcon::Application ? Lang::Text(Key::DisguiseTrayFollow)
				: icon == TrayIcon::Telegram ? u"Telegram"_q : u"SeeGram"_q;
		});
	::Settings::AddButtonWithLabel(content,
		Lang::Value(Key::DisguiseTray), trayValue, st::settingsButton,
		{ &st::menuIconPhoto })->addClickHandler([=] {
		controller->show(Box([](not_null<Ui::GenericBox*> box) {
			SingleChoiceBox(box, {
				.title = Lang::Value(Key::DisguiseTray),
				.options = { Lang::Text(Key::DisguiseTrayFollow), u"SeeGram"_q, u"Telegram"_q },
				.initialSelection = int(Store().disguiseSettings().trayIcon),
				.callback = [](int index) {
					auto settings = Store().disguiseSettings();
					settings.trayIcon = TrayIcon(index);
					if (!Store().setDisguiseSettings(settings)) return;
				},
			});
		}));
	});
	Ui::AddSkip(content);
	SettingsRows::AddDescription(content, Lang::Value(Key::DisguiseCleanAbout));
	Ui::AddSkip(content);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> DisguiseSection::title() {
	return Lang::Value(Key::DisguiseTitle);
}

} // namespace

::Settings::Type SectionId() {
	return DisguiseSection::Id();
}

} // namespace Fork::Disguise

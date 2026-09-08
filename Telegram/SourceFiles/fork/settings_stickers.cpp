/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/settings_stickers.h"
#include "fork/disguise.h"

#include "fork/fork_lang.h"
#include "fork/settings_rows.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "settings/settings_common_session.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Fork::Stickers {
namespace {

using Lang::Key;
auto RecentLimit = kDefaultRecentLimit;
rpl::event_stream<> Updated;

[[nodiscard]] QString FilePath() {
	return cWorkingDir() + u"tdata/fork_stickers.json"_q;
}

[[nodiscard]] int ServerMaximum(not_null<Main::Session*> session) {
	const auto &sets = session->data().stickers().sets();
	const auto i = sets.find(Data::Stickers::CloudRecentSetId);
	const auto received = (i == end(sets)) ? 0 : int(i->second->stickers.size());
	return std::max({ kMinimumRecentLimit,
		session->serverConfig().stickersRecentLimit, received });
}

void SetLimit(int limit) {
	if (limit < 0 || (limit > 0 && limit < kMinimumRecentLimit)
		|| limit == RecentLimit) {
		return;
	}
	RecentLimit = limit;
	auto file = QSaveFile(FilePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(QJsonObject{
			{ u"recentLimit"_q, limit },
		}).toJson());
		if (!file.commit()) {
			LOG(("Stickers: cannot save SeeGram preferences."));
		}
	} else {
		LOG(("Stickers: cannot open SeeGram preferences."));
	}
	Updated.fire({});
}

[[nodiscard]] QString RangeText(not_null<Main::Session*> session) {
	return Lang::Text(Key::StickersRange)
		.replace(u"{min}"_q, QString::number(kMinimumRecentLimit))
		.replace(u"{max}"_q, QString::number(ServerMaximum(session)))
		.replace(u"{default}"_q, QString::number(kDefaultRecentLimit));
}

void EditLimit(not_null<Ui::GenericBox*> box, not_null<Main::Session*> session) {
	box->setTitle(Lang::Value(Key::StickersRecent));
	const auto maximum = ServerMaximum(session);
	const auto initial = std::min(RecentLimit ? RecentLimit : kDefaultRecentLimit, maximum);
	const auto wrap = box->addRow(object_ptr<Ui::FixedHeightWidget>(
		box, st::defaultInputField.heightMin));
	const auto field = Ui::CreateChild<Ui::NumberInput>(
		wrap, st::defaultInputField, Lang::Value(Key::StickersRecent),
		QString::number(initial), maximum);
	wrap->widthValue() | rpl::on_next([=](int width) {
		field->resize(width, field->height());
	}, wrap->lifetime());
	box->addRow(object_ptr<Ui::FlatLabel>(box,
		rpl::single(RangeText(session)), st::boxDividerLabel));
	const auto save = [=] {
		const auto value = field->getLastText().toInt();
		if (value < kMinimumRecentLimit || value > ServerMaximum(session)) {
			field->showError();
			return;
		}
		SetLimit(value);
		box->closeBox();
	};
	QObject::connect(field, &Ui::NumberInput::submitted, box, save);
	box->addButton(Lang::Value(Key::Save), save);
	box->addButton(Lang::Value(Key::SeeTgCommentsCancel), [=] { box->closeBox(); });
	box->setFocusCallback([=] { field->setFocusFast(); field->selectAll(); });
}

class Section final : public ::Settings::Section<Section> {
public:
	Section(QWidget *parent, not_null<Window::SessionController*> controller)
	: Settings::Section<Section>(parent, controller) {
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(content, Lang::Value(Key::StickersRecent));
		const auto updates = rpl::merge(Changes(), Lang::Changes(),
			controller->session().data().stickers().updated(Data::StickersType::Stickers));
		const auto label = rpl::single(rpl::empty)
			| rpl::then(rpl::duplicate(updates)) | rpl::map([=] {
				return RecentLimit ? QString::number(std::min(RecentLimit,
					ServerMaximum(&controller->session()))) : Lang::Text(Key::StickersServer);
			});
		::Settings::AddButtonWithLabel(content, Lang::Value(Key::StickersRecent),
			label, st::settingsButtonNoIcon)->addClickHandler([=] {
			controller->show(Box(EditLimit, &controller->session()));
		});
		const auto server = content->add(object_ptr<Ui::SettingsButton>(
			content, Lang::Value(Key::StickersServer), st::settingsButtonNoIcon));
		server->toggleOn(rpl::single(rpl::empty)
			| rpl::then(Changes()) | rpl::map([] { return !RecentLimit; }));
		server->toggledChanges() | rpl::on_next([](bool enabled) {
			SetLimit(enabled ? 0 : kDefaultRecentLimit);
		}, server->lifetime());
		Ui::AddSkip(content);
		SettingsRows::AddDescription(content, rpl::single(rpl::empty)
			| rpl::then(rpl::duplicate(updates)) | rpl::map([=] {
				return RangeText(&controller->session());
			}));
		Ui::ResizeFitChild(this, content);
	}

	rpl::producer<QString> title() override {
		return Lang::Value(Key::StickersTitle);
	}

};

} // namespace

void Start() {
	auto file = QFile(FilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto value = QJsonDocument::fromJson(file.readAll()).object()
		.value(u"recentLimit"_q).toInt(kDefaultRecentLimit);
	RecentLimit = (value == 0 || value >= kMinimumRecentLimit)
		? value : kDefaultRecentLimit;
}

rpl::producer<> Changes() {
	return Updated.events();
}

::Settings::Type SectionId() {
	return Section::Id();
}

int RecentDisplayLimit(not_null<Main::Session*> session, bool masks,
		bool upstreamUnlimited, int upstreamLimit) {
	if (masks || Disguise::Clean()) {
		return upstreamUnlimited ? std::numeric_limits<int>::max() : upstreamLimit;
	}
	return RecentLimit ? std::min(RecentLimit, ServerMaximum(session))
		: std::numeric_limits<int>::max();
}

} // namespace Fork::Stickers

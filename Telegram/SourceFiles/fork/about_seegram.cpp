/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/about_seegram.h"
#include "fork/disguise.h"

#include "fork/build_counter.h"
#include "fork/fork_lang.h"
#include "boxes/about_box.h"
#include "core/click_handler_types.h"
#include "core/application.h"
#include "core/version.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_about_seegram.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace Fork::About {
namespace {

using Lang::Key;
constexpr auto kSource = "https://github.com/seegram/seegram-desktop";
constexpr auto kReleases = "https://github.com/seegram/seegram-desktop/releases";

[[nodiscard]] QString SeeGramVersion() {
	return Disguise::Name() + QChar(' ') + VersionText();
}

[[nodiscard]] QString TelegramVersion() {
	return u"Telegram "_q + currentVersionShortText();
}

[[nodiscard]] TextWithEntities Links() {
	return tr::link(u"GitHub"_q, QString::fromLatin1(kSource))
		.append(u" · "_q)
		.append(tr::link(u"README"_q, QString::fromLatin1(kSource) + u"#readme"_q))
		.append(u" · "_q)
		.append(tr::link(Lang::Text(Key::AboutReleases), QString::fromLatin1(kReleases)))
		.append(u" · "_q)
		.append(tr::link(Lang::Text(Key::NewsChannel), u"https://t.me/seeclient"_q));
}

} // namespace

QString VersionText() {
	return QString::fromLatin1(AppVersionStr)
		+ u" (build "_q + QString::number(BuildCounter) + ')';
}

void Fill(not_null<Ui::GenericBox*> box) {
	box->setTitle(Lang::Value(Key::AboutTitle));
	box->setWidth(st::seegramAboutWidth);
	const auto header = box->addRow(object_ptr<Ui::RpWidget>(box));
	header->resize(header->width(), st::seegramAboutHeader);
	const auto logo = Ui::CreateChild<Ui::RpWidget>(header);
	logo->resize(st::seegramAboutLogo, st::seegramAboutLogo);
	logo->paintRequest() | rpl::on_next([=] {
		const auto &image = Disguise::Image();
		auto p = QPainter(logo);
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(logo->rect(), image);
	}, logo->lifetime());
	const auto title = Ui::CreateChild<Ui::FlatLabel>(header,
		Disguise::NameValue(), st::boxTitle);
	const auto version = Ui::CreateChild<Ui::LinkButton>(header,
		VersionText(), st::aboutVersionLink);
	version->addClickHandler([] { UrlClickHandler::Open(QString::fromLatin1(kReleases)); });
	const auto upstream = Ui::CreateChild<Ui::LinkButton>(header,
		TelegramVersion(), st::aboutVersionLink);
	upstream->addClickHandler([] { UrlClickHandler::Open(Core::App().changelogLink()); });
	header->widthValue() | rpl::on_next([=](int width) {
		const auto left = st::seegramAboutLogo + st::seegramAboutGap;
		logo->moveToLeft(0, 0, width);
		title->resizeToWidth(std::max(1, width - left));
		title->moveToLeft(left, 0, width);
		version->moveToLeft(left, title->height() + st::seegramAboutVersionGap, width);
		upstream->moveToLeft(left,
			version->y() + version->height() + st::seegramAboutVersionGap, width);
	}, header->lifetime());
	const auto addText = [&](rpl::producer<QString> text, bool note = false) {
		box->addRow(object_ptr<Ui::FlatLabel>(box, std::move(text),
			note ? st::seegramAboutNote : st::seegramAboutText));
		Ui::AddSkip(box->verticalLayout(), st::seegramAboutGap);
	};
	addText(Lang::Value(Key::AboutSummary));
	addText(Lang::Value(Key::AboutSeeTg));
	addText(Lang::Value(Key::AboutHistory), true);
	const auto links = box->addRow(object_ptr<Ui::FlatLabel>(box,
		rpl::single(rpl::empty) | rpl::then(Lang::Changes())
			| rpl::map([] { return Links(); }), st::seegramAboutText));
	links->setLinksTrusted();
	Ui::AddSkip(box->verticalLayout(), st::seegramAboutGap);
	const auto credits = box->addRow(object_ptr<Ui::FlatLabel>(box,
		rpl::single(tr::link(u"Telegram Desktop"_q, u"https://github.com/telegramdesktop/tdesktop"_q)
			.append(u" · "_q).append(tr::link(u"AyuGram"_q, u"https://github.com/AyuGram/AyuGramDesktop"_q))
			.append(u"\n"_q).append(tr::link(u"GPLv3 + OpenSSL exception"_q,
				QString::fromLatin1(kSource) + u"/blob/main/LICENSE"_q))), st::seegramAboutNote));
	credits->setLinksTrusted();
	box->addButton(Lang::Value(Key::Close), [=] { box->closeBox(); });
}

void SetupFooter(not_null<Ui::FlatLabel*> seegram,
		not_null<Ui::FlatLabel*> telegram,
		not_null<Window::SessionController*> controller) {
	if (Disguise::Clean()) return;
	const auto update = [=] {
		seegram->setMarkedText(tr::link(SeeGramVersion(), QString::fromLatin1(kReleases)));
		seegram->setLinksTrusted();
		telegram->setMarkedText(tr::link(TelegramVersion(), 1)
			.append(u" · "_q).append(tr::link(Lang::Text(Key::AboutTitle), 2)));
		telegram->setLink(1, std::make_shared<UrlClickHandler>(Core::App().changelogLink()));
		telegram->setLink(2, std::make_shared<LambdaClickHandler>([=] {
			controller->show(Box(Fill));
		}));
	};
	update();
	rpl::merge(Lang::Changes(), Disguise::Changes())
		| rpl::on_next(update, telegram->lifetime());
}

} // namespace Fork::About

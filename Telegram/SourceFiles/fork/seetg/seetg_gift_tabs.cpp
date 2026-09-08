#include "fork/seetg/seetg_gift_tabs.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_comments.h"
#include "fork/seetg/seetg_history.h"
#include "fork/seetg/seetg_comments_data.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_visuals.h"
#include "fork/seetg/seetg_peers.h"
#include "fork/fork_lang.h"
#include "fork/seetg/seetg_types.h"
#include "lang/lang_keys.h"
#include "chat_helpers/compose/compose_show.h"
#include "window/window_session_controller.h"
#include "data/data_peer.h"
#include "data/data_star_gift.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_seetg_comments.h"
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtGui/QPainterPath>
#include <cmath>

namespace Fork::SeeTg::GiftTabs {
namespace {
using Lang::Key;
QString Money(const QString &amount, QString currency) {
	currency = currency.toLower();
	const auto divisor = currency == u"xtr" ? 1. : currency == u"usdt" ? 1e6 : 1e9;
	const auto value = amount.toDouble() / divisor;
	if (!std::isfinite(value) || value < 0) return {};
	auto number = QLocale().toString(value, 'f', currency == u"xtr" ? 0 : 2);
	if (currency != u"xtr") {
		while (number.endsWith('0')) {
			number.chop(1);
		}
		const auto decimalPoint = QString(QLocale().decimalPoint());
		if (number.endsWith(decimalPoint)) {
			number.chop(decimalPoint.size());
		}
	}
	return number + ' '
		+ ((currency == u"gram" || currency == u"ton") ? u"TON"_q : currency == u"xtr" ? u"★"_q : currency.toUpper());
}
QString Changed(const QString &text) {
	static const auto regex = QRegularExpression(u"^(\\d+)\\s+([a-zA-Z]+)$"_q);
	const auto match = regex.match(text);
	return match.hasMatch() ? Money(match.captured(1), match.captured(2)) : text;
}
Key EventTitle(const QJsonObject &item) {
	if (item[u"hidden"_q].toBool()) return Key::SeeTgHistoryHiddenEvent;
	const auto sale = item[u"saleAction"_q].toObject();
	if (!sale.isEmpty()) return Key::GiftEventSale;
	const auto action = item[u"giftAction"_q].toObject();
	const auto kind = action[u"action"_q].toString();
	if (kind == u"transfer") return action[u"direction"_q].toString() == u"SENT" ? Key::SeeTgHistoryGiftSent : Key::SeeTgHistoryGiftReceived;
	if (kind == u"moved") return Key::SeeTgHistoryGiftMoved;
	if (kind == u"price") return Key::GiftEventPrice;
	if (kind == u"listing") return Key::GiftEventListing;
	if (kind == u"delisting") return Key::GiftEventDelisting;
	if (kind == u"mint") return Key::GiftEventMint;
	if (kind == u"new") return Key::GiftEventNew;
	return Key::GiftEventUnknown;
}

class History final : public Ui::VerticalLayout {
public:
	History(QWidget *parent, not_null<Window::SessionController*> controller, QString giftId, QJsonObject gift)
	: VerticalLayout(parent), _controller(controller), _giftId(std::move(giftId)), _gift(std::move(gift)) {
		_rows = add(object_ptr<Ui::VerticalLayout>(this));
		_rows->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(_rows);
			p.fillRect(_rows->rect(), st::boxDividerBg->c);
		}, _rows->lifetime());
		_status = add(object_ptr<Ui::FlatLabel>(this, QString(), st::seetgCommentMeta), st::boxRowPadding);
		_more = add(object_ptr<Ui::LinkButton>(this, Lang::Text(Key::SeeTgHistoryMore), st::defaultLinkButton), st::boxRowPadding);
		_more->setClickedCallback([=] { load(); });
		load();
	}
private:
	void append(QJsonObject item) {
		if (!item[u"hidden"_q].toBool() && item[u"giftAction"_q].toObject().isEmpty()) {
			item[u"giftAction"_q] = QJsonObject{{u"action"_q, u"sale"_q}, {u"gift"_q, _gift}};
		}
		const auto id = item[u"id"_q].toString();
		if (id.isEmpty() || _seen.contains(id)) return;
		_seen.insert(id);
		auto details = QStringList();
		if (!item[u"hidden"_q].toBool()) {
			const auto action = item[u"giftAction"_q].toObject();
			const auto sale = item[u"saleAction"_q].toObject();
			if (!sale.isEmpty()) {
				details.push_back(Lang::Text(Key::GiftHistoryPriceLabel) + u": "_q + Money(sale[u"amount"_q].toString(), sale[u"currency"_q].toString()));
				auto market = sale[u"market"_q].toString();
				if (!market.isEmpty()) { market[0] = market[0].toUpper(); details.push_back(Lang::Text(Key::GiftHistoryMarketLabel) + u": "_q + market); }
			}
			const auto kind = action[u"action"_q].toString();
			if (kind == u"price" || kind == u"listing" || kind == u"delisting") {
				const auto before = Changed(action[u"oldValue"_q].toString());
				const auto after = Changed(action[u"newValue"_q].toString());
				if (!before.isEmpty() || !after.isEmpty()) details.push_back(Lang::Text(kind == u"price" ? Key::GiftHistoryPriceLabel : Key::GiftHistoryMarketLabel) + u": "_q + (before.isEmpty() ? after : after.isEmpty() ? before : before + u" → "_q + after));
			}
			const auto field = action[u"field"_q].toString();
			if (!field.isEmpty() && kind != u"price") details.push_back(field);
		}
		_rows->add(Fork::SeeTg::History::GiftEventRow(_rows, _controller,
			item, Lang::Text(EventTitle(item)), std::move(details)));
	}
	void load() {
		if (_loading || !_hasMore) return;
		_loading = true;
		_more->hide();
		_status->setText(Lang::Text(Key::SeeTgLoading));
		Api::FreshQuery(&_controller->session(),
			u"query GiftHistory($id:String!,$after:String){giftHistory(giftId:$id,first:30,after:$after,sortDir:DESC){items{id time hidden saleAction{market kind amount currency} giftAction{action direction field oldValue newValue from{seeId telegramId telegramType username usernames name title address} to{seeId telegramId telegramType username usernames name title address} gift{id giftId title slug num model{name} backdrop{name} pattern{name}}}}pageInfo{endCursor hasNextPage}}}"_q,
			{{u"id"_q, _giftId}, {u"after"_q, _cursor.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(_cursor)}},
			crl::guard(this, [=](const QJsonObject &data) {
				_loading = false;
				const auto page = data[u"giftHistory"_q].toObject();
				for (const auto &item : page[u"items"_q].toArray()) append(item.toObject());
				const auto info = page[u"pageInfo"_q].toObject();
				const auto next = info[u"endCursor"_q].toString();
				_hasMore = info[u"hasNextPage"_q].toBool() && !next.isEmpty() && next != _cursor;
				_cursor = next;
				_status->setText(_seen.empty() ? Lang::Text(Key::SeeTgHistoryEmpty) : QString());
				_more->setText(Lang::Text(Key::SeeTgHistoryMore));
				_more->setVisible(_hasMore);
			}), crl::guard(this, [=](const Api::Error&) {
				_loading = false;
				_status->setText(Lang::Text(Key::SeeTgHistoryError));
				_more->setText(Lang::Text(Key::SeeTgCommentsRetry));
				_more->show();
			}));
	}
	not_null<Window::SessionController*> _controller;
	QString _giftId;
	QJsonObject _gift;
	QString _cursor;
	base::flat_set<QString> _seen;
	bool _loading = false;
	bool _hasMore = true;
	Ui::VerticalLayout *_rows = nullptr;
	Ui::FlatLabel *_status = nullptr;
	Ui::LinkButton *_more = nullptr;
};
} // namespace

not_null<Ui::VerticalLayout*> Add(not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> content,
		std::shared_ptr<ChatHelpers::Show> show, const Data::UniqueGift &gift) {
	const auto giftId = QString::number(gift.id);
	const auto giftData = QJsonObject{
		{u"giftId"_q, QString::number(gift.initialGiftId)},
		{u"title"_q, gift.title}, {u"slug"_q, gift.slug.section('-', 0, -2)},
		{u"num"_q, gift.number},
		{u"model"_q, QJsonObject{{u"name"_q, gift.model.name}}},
		{u"backdrop"_q, QJsonObject{{u"name"_q, gift.backdrop.name}}},
		{u"pattern"_q, QJsonObject{{u"name"_q, gift.pattern.name}}}};
	const auto controller = show->resolveWindow();
	if (!Enabled() || !controller || giftId.isEmpty() || giftId == u"0") return content;
	const auto tabs = content->add(object_ptr<Ui::SettingsSlider>(content, st::defaultTabsSlider), st::boxRowPadding);
	Ui::AddSkip(content, st::defaultVerticalListSkip);
	tabs->setSections(std::vector<QString>{Lang::Text(Key::GiftInfoTab), Lang::Text(Key::GiftHistoryTab), Lang::Text(Key::GiftCommentsTab)});
	using Pane = Ui::SlideWrap<Ui::VerticalLayout>;
	struct State { std::array<Pane*, 3> panes; std::array<bool, 3> loaded = {true, false, false}; };
	const auto state = content->lifetime().make_state<State>();
	for (auto i = 0; i != 3; ++i) {
		state->panes[i] = content->add(object_ptr<Pane>(content, object_ptr<Ui::VerticalLayout>(content)));
		state->panes[i]->toggle(i == 0, anim::type::instant);
	}
	tabs->sectionActivated() | rpl::on_next([=](int index) {
		if (index < 0 || index > 2) return;
		const auto pane = state->panes[index]->entity();
		if (!state->loaded[index]) {
			state->loaded[index] = true;
			if (index == 1) pane->add(object_ptr<History>(pane, controller, giftId, giftData));
			else if (index == 2) pane->add(Comments::ForGift(pane, controller, giftId,
				crl::guard(box, [=] { box->scrollToWidget(pane); })));
		}
		for (auto i = 0; i != 3; ++i) state->panes[i]->toggle(i == index, anim::type::instant);
	}, content->lifetime());
	return state->panes[0]->entity();
}
} // namespace Fork::SeeTg::GiftTabs

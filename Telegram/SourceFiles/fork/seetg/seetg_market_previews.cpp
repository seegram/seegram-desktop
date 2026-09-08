#include "fork/seetg/seetg_market_previews.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_visuals.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "ui/rp_widget.h"
#include <QtCore/QJsonArray>
#include <QtCore/QLocale>
#include <QtCore/QPointer>
#include <cmath>

namespace Fork::SeeTg::MarketPreviews {
namespace {
using Button = Info::PeerGifts::GiftButton;
struct Preview {
	QString price;
	QString userpic;
	QImage logo;
	TimeId expires = 0;
};
struct Binding { QPointer<Button> button; QString slug; };
struct State {
	base::flat_map<QString, Preview> cache;
	std::vector<Binding> bindings;
	base::flat_set<QString> pending;
	base::Timer timer;
	bool busy = false;
};
Preview Parse(const QJsonObject &object) {
	auto result = Preview{ .expires = base::unixtime::now() + 120 };
	if (!object[u"onSale"_q].toBool()) return result;
	for (const auto &item : object[u"saleInfo"_q].toArray()) {
		const auto sale = item.toObject();
		const auto market = sale[u"market"_q].toString();
		const auto user = market == u"portals" ? u"portals"_q
			: market == u"tonnel" ? u"tonnel_relayer_bot"_q
			: market == u"getgems" ? u"getgems"_q
			: market == u"mrkt" ? u"mrkt"_q : QString();
		const auto currency = sale[u"currency"_q].toString().toLower();
		const auto ton = currency == u"ton" || currency == u"gram";
		if (user.isEmpty() || (!ton && currency != u"usdt" && currency != u"major"
			&& currency != u"not" && currency != u"dogs")) continue;
		const auto amount = sale[u"amount"_q].toString().toDouble() / (currency == u"usdt" ? 1e6 : 1e9);
		if (!std::isfinite(amount) || amount <= 0) continue;
		result.price = QLocale().toString(amount, 'f', amount >= 1000 ? 0 : amount >= 100 ? 1 : 2)
			+ ' ' + (ton ? u"TON"_q : currency.toUpper());
		result.userpic = Visuals::UserpicUrl(user);
		break;
	}
	return result;
}
void Apply(State *state) {
	std::erase_if(state->bindings, [](const Binding &binding) { return !binding.button; });
	for (const auto &binding : state->bindings) {
		const auto i = state->cache.find(binding.slug);
		if (Enabled(Feature::MarketPreviews) && i != end(state->cache)) {
			binding.button->setExternalSale(i->second.price, i->second.logo);
		} else {
			binding.button->setExternalSale({}, {});
		}
	}
}
} // namespace

Bind Create(not_null<Ui::RpWidget*> owner, not_null<Main::Session*> session) {
	const auto state = owner->lifetime().make_state<State>();
	state->timer.setCallback([=] {
		if (state->busy || state->pending.empty() || !Enabled(Feature::MarketPreviews)) return;
		auto requested = QStringList();
		auto input = QJsonArray();
		while (!state->pending.empty() && requested.size() < 100) {
			const auto slug = *state->pending.begin();
			state->pending.erase(state->pending.begin());
			if (!ranges::any_of(state->bindings, [&](const Binding &b) { return b.button && b.slug == slug; })) continue;
			requested.push_back(slug);
			input.push_back(slug);
		}
		if (requested.isEmpty()) return;
		state->busy = true;
		Api::FreshQuery(session,
			u"query GiftMarketPreviews($slugs:[String!]!){giftMarketPreviews(slugs:$slugs){slug onSale saleInfo{market amount currency}}}"_q,
			{{u"slugs"_q, input}},
			crl::guard(owner, [=](const QJsonObject &data) {
				state->busy = false;
				for (const auto &slug : requested) {
					state->pending.erase(slug);
					state->cache[slug].expires = base::unixtime::now() + 120;
				}
				for (const auto &item : data[u"giftMarketPreviews"_q].toArray()) {
					const auto object = item.toObject();
					const auto slug = object[u"slug"_q].toString();
					if (!requested.contains(slug)) continue;
					auto preview = Parse(object);
					const auto url = preview.userpic;
					state->cache[slug] = std::move(preview);
					if (!url.isEmpty()) Visuals::Image(url, crl::guard(owner, [=](QImage image) {
						const auto i = state->cache.find(slug);
						if (i != end(state->cache) && i->second.userpic == url) {
							i->second.logo = std::move(image);
							Apply(state);
						}
					}));
				}
				Apply(state);
				if (!state->pending.empty()) state->timer.callOnce(100);
			}),
			crl::guard(owner, [=](const Api::Error&) {
				state->busy = false;
				// Retain existing previews on transient failures and back off.
				for (const auto &slug : requested) {
					state->pending.erase(slug);
					state->cache[slug].expires = base::unixtime::now() + 30;
				}
				if (!state->pending.empty()) state->timer.callOnce(100);
			}));
	});
	EnabledValue(Feature::MarketPreviews) | rpl::on_next([=](bool enabled) {
		if (!enabled) {
			state->pending.clear();
			state->timer.cancel();
		} else {
			for (const auto &binding : state->bindings) if (binding.button && !binding.slug.isEmpty()) state->pending.insert(binding.slug);
			state->timer.callOnce(100);
		}
		Apply(state);
	}, owner->lifetime());
	return [=](not_null<Button*> button, const QString &slug) {
		auto i = ranges::find_if(state->bindings, [=](const Binding &b) { return b.button == button.get(); });
		if (i == end(state->bindings)) state->bindings.push_back({ button.get(), slug });
		else i->slug = slug;
		Apply(state);
		if (!Enabled(Feature::MarketPreviews) || slug.isEmpty()) return;
		const auto cached = state->cache.find(slug);
		if (cached != end(state->cache) && cached->second.expires > base::unixtime::now()) return;
		state->pending.insert(slug);
		if (!state->timer.isActive() && !state->busy) state->timer.callOnce(100);
	};
}
} // namespace Fork::SeeTg::MarketPreviews

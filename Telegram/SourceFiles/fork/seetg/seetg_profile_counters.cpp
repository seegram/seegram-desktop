#include "fork/seetg/seetg_profile_counters.h"

#include "fork/fork_lang.h"
#include "fork/fork_plural.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_types.h"
#include "base/weak_ptr.h"
#include "data/data_peer.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "styles/style_info.h"

#include <rpl/variable.h>
#include <QtCore/QLocale>

namespace Fork::SeeTg::Counters {
namespace {

constexpr auto kTtl = crl::time(30000);
constexpr auto kDocument = "query ProfileCounters($id: String!) {"
	" profileCounters(seeId: $id) { giftTransfers comments } }";

struct Counts {
	std::optional<int> transfers;
	std::optional<int> comments;
	friend bool operator==(const Counts &, const Counts &) = default;
};

struct State {
	base::weak_ptr<Main::Session> session;
	QString seeId;
	rpl::variable<Counts> counts;
	crl::time expires = 0;
	bool loading = false;
	bool invalidated = false;
};

base::flat_map<QString, std::weak_ptr<State>> States;

QString StateKey(not_null<Main::Session*> session, const QString &seeId) {
	return QString::number(session->uniqueId()) + '/' + seeId;
}

void Request(const std::shared_ptr<State> &state) {
	if (!state->session || (!Enabled(Feature::Transfers) && !Enabled(Feature::Comments)) || state->loading
		|| state->expires > crl::now()) {
		return;
	}
	state->loading = true;
	state->invalidated = false;
	Api::FreshQuery(state->session.get(), QString::fromLatin1(kDocument), {
		{ u"id"_q, state->seeId },
	}, [state](const QJsonObject &data) {
		state->loading = false;
		if (state->invalidated) {
			Request(state);
			return;
		}
		const auto object = data.value(u"profileCounters"_q).toObject();
		const auto read = [&](const QString &key) -> std::optional<int> {
			const auto value = object.value(key);
			const auto count = value.toInt(-1);
			return count >= 0 ? std::make_optional(count) : std::nullopt;
		};
		state->expires = crl::now() + kTtl;
		state->counts = Counts{
			read(u"giftTransfers"_q),
			read(u"comments"_q),
		};
	}, [state](const Api::Error &) {
		state->loading = false;
		if (state->invalidated) {
			Request(state);
		}
	});
}

std::shared_ptr<State> Get(not_null<PeerData*> peer) {
	for (auto i = States.begin(); i != States.end();) {
		if (i->second.expired()) {
			i = States.erase(i);
		} else {
			++i;
		}
	}
	const auto seeId = SeeId(peer->id);
	auto &weak = States[StateKey(&peer->session(), seeId)];
	if (const auto state = weak.lock()) {
		return state;
	}
	const auto state = std::make_shared<State>();
	state->session = base::make_weak(&peer->session());
	state->seeId = seeId;
	weak = state;
	return state;
}

QString Text(Kind kind, const Counts &counts) {
	using Key = Lang::Key;
	const auto count = (kind == Kind::Transfers)
		? counts.transfers : counts.comments;
	if (!count) {
		return Lang::Text(kind == Kind::Transfers
			? Key::SeeTgHistoryButton : Key::SeeTgCommentsTab);
	}
	const auto keys = (kind == Kind::Transfers) ? std::array{
		Key::SeeTgTransfersZero, Key::SeeTgTransfersOne,
		Key::SeeTgTransfersTwo, Key::SeeTgTransfersFew,
		Key::SeeTgTransfersMany, Key::SeeTgTransfersOther,
	} : std::array{
		Key::SeeTgCommentsZero, Key::SeeTgCommentsOne,
		Key::SeeTgCommentsTwo, Key::SeeTgCommentsFew,
		Key::SeeTgCommentsMany, Key::SeeTgCommentsOther,
	};
	const auto language = Lang::ResolvedId();
	const auto form = Lang::IntegerPlural(language.toStdString(), *count);
	return Lang::Text(keys[int(form)]).replace(
		u"{count}"_q, QLocale(language).toString(*count));
}

} // namespace

rpl::producer<QString> Label(not_null<PeerData*> peer, Kind kind) {
	if (peer->isSelf()) {
		return Lang::Value(kind == Kind::Transfers
			? Lang::Key::SeeTgHistoryButton : Lang::Key::SeeTgCommentsTab);
	}
	const auto state = Get(peer);
	return rpl::combine(
		state->counts.value(),
		rpl::single(rpl::empty) | rpl::then(Lang::Changes()),
		EnabledValue(kind == Kind::Transfers ? Feature::Transfers : Feature::Comments)
	) | rpl::map([state, kind](const Counts &counts, auto, bool enabled) {
		if (enabled) {
			Request(state);
		}
		return Text(kind, enabled ? counts : Counts{});
	}) | rpl::distinct_until_changed();
}

void AddRightLabel(
		not_null<Ui::SettingsButton*> button,
		not_null<PeerData*> peer,
		Kind kind) {
	if (!peer->isSelf()) {
		return;
	}
	const auto state = Get(peer);
	auto value = rpl::combine(
		state->counts.value(),
		rpl::single(rpl::empty) | rpl::then(Lang::Changes()),
		EnabledValue(kind == Kind::Transfers ? Feature::Transfers : Feature::Comments)
	) | rpl::map([state, kind](const Counts &counts, auto, bool enabled) {
		if (enabled) {
			Request(state);
		}
		const auto count = (kind == Kind::Transfers)
			? counts.transfers : counts.comments;
		return (enabled && count)
			? QLocale(Lang::ResolvedId()).toString(*count)
			: QString();
	}) | rpl::distinct_until_changed();
	::Settings::CreateRightLabel(
		button,
		std::move(value),
		st::infoSharedMediaButton,
		Label(peer, kind));
}

void Invalidate(not_null<Main::Session*> session, const QString &seeId) {
	const auto i = States.find(StateKey(session, seeId));
	if (i == States.end()) {
		return;
	}
	if (const auto state = i->second.lock()) {
		state->expires = 0;
		state->invalidated = true;
		Request(state);
	}
}

} // namespace Fork::SeeTg::Counters

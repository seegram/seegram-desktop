#include "fork/seetg/seetg_reactions.h"

#include "fork/fork_lang.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_types.h"
#include "base/weak_ptr.h"
#include "data/data_peer.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QLocale>
#include <rpl/variable.h>

namespace Fork::SeeTg::Reactions {
namespace {

struct Snapshot {
	int likes = 0;
	int poops = 0;
	QString mine;
	bool ready = false;
	bool busy = false;
	friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

struct ReactionState {
	base::weak_ptr<Main::Session> session;
	QString seeId;
	rpl::variable<Snapshot> value;
	rpl::event_stream<> errors;
};

base::flat_map<QString, std::weak_ptr<ReactionState>> States;

std::optional<Snapshot> Parse(const QJsonValue &value) {
	if (!value.isObject()) return std::nullopt;
	const auto object = value.toObject();
	const auto likes = object.value(u"likes"_q).toInt(-1);
	const auto poops = object.value(u"poops"_q).toInt(-1);
	const auto mine = object.value(u"mine"_q).toString();
	if (likes < 0 || poops < 0
		|| (!mine.isEmpty() && mine != u"like"_q && mine != u"poop"_q)) {
		return std::nullopt;
	}
	return Snapshot{ likes, poops, mine, true, false };
}

void Load(const std::shared_ptr<ReactionState> &state) {
	if (!state->session || !Enabled(Feature::Reactions) || state->value.current().busy) return;
	auto current = state->value.current();
	current.busy = true;
	state->value = current;
	const auto failed = [state](const Api::Error&) {
		auto current = state->value.current();
		current.busy = false;
		current.ready = false;
		state->value = current;
	};
	Api::FreshQuery(state->session.get(),
		u"query OwnerReactions($seeId: String!) { ownerReactions(seeId: $seeId) { likes poops mine } }"_q,
		{{ u"seeId"_q, state->seeId }},
		[state, failed](const QJsonObject &data) {
			if (const auto value = Parse(data.value(u"ownerReactions"_q))) {
				state->value = *value;
			} else {
				failed(Api::Error{});
			}
		}, failed);
}

void React(const std::shared_ptr<ReactionState> &state, const QString &kind) {
	if (!state->session || !Enabled(Feature::Reactions) || state->value.current().busy) return;
	if (!state->value.current().ready) {
		Load(state);
		return;
	}
	const auto before = state->value.current();
	const auto next = (before.mine == kind) ? QString() : kind;
	auto optimistic = before;
	optimistic.likes = std::max(0, before.likes - (before.mine == u"like"_q) + (next == u"like"_q));
	optimistic.poops = std::max(0, before.poops - (before.mine == u"poop"_q) + (next == u"poop"_q));
	optimistic.mine = next;
	optimistic.busy = true;
	state->value = optimistic;
	const auto failed = [state, before](const Api::Error&) {
		auto restored = before;
		restored.ready = false;
		state->value = restored;
		state->errors.fire({});
		// A lost response does not mean the write failed. Reconcile once,
		// without replaying the mutation or risking an accidental toggle.
		Load(state);
	};
	Api::Mutation(state->session.get(),
		u"mutation ReactOwner($seeId: String!, $reaction: String) { reactToOwner(seeId: $seeId, reaction: $reaction) { likes poops mine } }"_q,
		{{ u"seeId"_q, state->seeId },
		 { u"reaction"_q, next.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(next) }},
		[state, failed](const QJsonObject &data) {
			if (const auto value = Parse(data.value(u"reactToOwner"_q))) {
				state->value = *value;
			} else {
				failed(Api::Error{});
			}
		}, failed);
}

std::shared_ptr<ReactionState> Get(not_null<PeerData*> peer) {
	for (auto i = States.begin(); i != States.end();) {
		i = i->second.expired() ? States.erase(i) : std::next(i);
	}
	const auto seeId = SeeId(peer->id);
	const auto key = QString::number(peer->session().uniqueId()) + '/' + seeId;
	auto &weak = States[key];
	if (const auto existing = weak.lock()) return existing;
	const auto state = std::make_shared<ReactionState>();
	state->session = base::make_weak(&peer->session());
	state->seeId = seeId;
	weak = state;
	return state;
}

class ReactionButton final : public Ui::RippleButton {
public:
	ReactionButton(QWidget *parent, std::shared_ptr<ReactionState> state, bool like)
	: RippleButton(parent, st::settingsButton.ripple)
	, _state(std::move(state))
	, _kind(like ? u"like"_q : u"poop"_q)
	, _image(like ? u":/fork/reactions/heart.png"_q : u":/fork/reactions/poop.png"_q) {
		resize(style::ConvertScale(42), style::ConvertScale(44));
		_state->value.value() | rpl::on_next([=](const Snapshot &value) {
			_value = value;
			setDisabled(value.busy);
			update();
			accessibilityNameChanged();
		}, lifetime());
		Lang::Value(like ? Lang::Key::ReactionLike : Lang::Key::ReactionDislike)
		| rpl::on_next([=](const QString &text) {
			_label = text;
			setToolTip(text);
			accessibilityNameChanged();
		}, lifetime());
		addClickHandler([=] { React(_state, _kind); });
	}
	QString accessibilityName() override {
		return _label + u": "_q + QString::number(count());
	}
protected:
	void paintEvent(QPaintEvent *event) override {
		auto p = Painter(this);
		p.setRenderHint(QPainter::Antialiasing);
		const auto selected = (_value.mine == _kind);
		auto background = selected ? st::windowActiveTextFg->c : st::windowBg->c;
		background.setAlphaF(selected ? 0.24 : isOver() ? 0.65 : 0.38);
		p.setPen(Qt::NoPen);
		p.setBrush(background);
		p.drawRoundedRect(rect(), style::ConvertScale(12), style::ConvertScale(12));
		paintRipple(p, 0, 0);
		const auto size = style::ConvertScale(23);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(QRect((width() - size) / 2, style::ConvertScale(3), size, size), _image);
		p.setFont(st::settingsExperimentalAbout.style.font);
		p.setPen(st::windowFg);
		const auto number = count();
		const auto text = !_value.ready ? u"—"_q
			: number >= 1000000 ? QLocale().toString(number / 1000000., 'f', 1) + u"M"_q
			: number >= 1000 ? QLocale().toString(number / 1000., 'f', 1) + u"K"_q
			: QString::number(number);
		p.drawText(QRect(0, style::ConvertScale(26), width(), style::ConvertScale(16)), Qt::AlignCenter, text);
	}
private:
	int count() const { return _kind == u"like"_q ? _value.likes : _value.poops; }
	const std::shared_ptr<ReactionState> _state;
	const QString _kind;
	const QImage _image;
	Snapshot _value;
	QString _label;
};

} // namespace

void Setup(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		rpl::producer<bool> shown,
		rpl::producer<bool> backShown) {
	if (!peer->isUser() && !peer->isChannel()) return;
	const auto state = Get(peer);
	const auto row = Ui::CreateChild<Ui::RpWidget>(parent.get());
	const auto like = Ui::CreateChild<ReactionButton>(row, state, true);
	const auto dislike = Ui::CreateChild<ReactionButton>(row, state, false);
	dislike->move(like->width() + style::ConvertScale(6), 0);
	row->resize(dislike->x() + dislike->width(), like->height());
	like->show();
	dislike->show();
	rpl::combine(std::move(shown), EnabledValue(Feature::Reactions), std::move(backShown))
	| rpl::on_next([=](bool visible, bool enabled, bool back) {
		row->move(style::ConvertScale(12), style::ConvertScale(back ? 54 : 6));
		row->setVisible(visible && enabled);
		row->raise();
		if (visible && enabled && !state->value.current().ready) Load(state);
	}, row->lifetime());
	state->errors.events() | rpl::on_next([=] {
		controller->showToast(Lang::Text(Lang::Key::SeeTgCommentsActionFailed));
	}, row->lifetime());
}

} // namespace Fork::SeeTg::Reactions

/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_peers.h"

#include "fork/fork_lang.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_visuals.h"
#include "chat_helpers/compose/compose_show.h"
#include "ui/wrap/table_layout.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "ui/controls/table_rows.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_giveaway.h"
#include "styles/style_userpic_button.h"
#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>

namespace Fork::SeeTg::Peers {
namespace {

// A peer that could not be resolved is not asked about again this session:
// the answer would be the same, and the username path is rationed.
base::flat_map<not_null<Main::Session*>, base::flat_set<PeerId>> Failed;

void Fail(
	not_null<Main::Session*> session,
	const Owner &owner,
	const Fn<void(PeerData*)> &done);

void ByUsername(
	not_null<Main::Session*> session,
	const Owner &owner,
	Fn<void(PeerData*)> done);

// The gift path came up empty: the username is tried only when the user
// allowed spending the daily ration on it. A peer the client has never met
// has no username of its own, so see.tg is asked for one first - without it
// there is nothing to look up.
void AfterGift(
		not_null<Main::Session*> session,
		const Owner &owner,
		Fn<void(PeerData*)> done) {
	if (!Current().usernameFallback) {
		Fail(session, owner, done);
		return;
	} else if (!owner.username.isEmpty() || owner.seeId.isEmpty()) {
		ByUsername(session, owner, std::move(done));
		return;
	}
	auto variables = QJsonObject();
	variables.insert(u"o"_q, owner.seeId);
	Api::Query(session, QString::fromLatin1(kQueryOwner), variables, [=](
			const QJsonObject &data) {
		auto found = ParseOwner(data.value(u"owner"_q).toObject());
		if (found.username.isEmpty()) {
			Fail(session, owner, done);
			return;
		}
		found.peerId = owner.peerId;
		ByUsername(session, found, done);
	}, [=](const Api::Error &) {
		Fail(session, owner, done);
	});
}

[[nodiscard]] PeerData *Loaded(
		not_null<Main::Session*> session,
		const Owner &owner) {
	return owner.known() ? session->data().peerLoaded(owner.peerId) : nullptr;
}

void Fail(
		not_null<Main::Session*> session,
		const Owner &owner,
		const Fn<void(PeerData*)> &done) {
	const auto i = Failed.find(session);
	if (i == end(Failed)) {
		Failed.emplace(session, base::flat_set<PeerId>{ owner.peerId });
		session->lifetime().add([=] {
			Failed.remove(session);
		});
	} else {
		i->second.emplace(owner.peerId);
	}
	done(nullptr);
}

void ByUsername(
		not_null<Main::Session*> session,
		const Owner &owner,
		Fn<void(PeerData*)> done) {
	if (owner.username.isEmpty()) {
		Fail(session, owner, done);
		return;
	}
	LOG(("SeeTg Info: resolving @%1 by username.").arg(owner.username));
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(owner.username),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		if (const auto peer = Loaded(session, owner)) {
			done(peer);
		} else {
			Fail(session, owner, done);
		}
	}).fail([=] {
		Fail(session, owner, done);
	}).send();
}

void ByGift(
		not_null<Main::Session*> session,
		const Owner &owner,
		const QString &slug,
		Fn<void(PeerData*)> done) {
	session->api().request(
		MTPpayments_GetUniqueStarGift(MTP_string(slug))
	).done([=](const MTPpayments_UniqueStarGift &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		if (const auto peer = Loaded(session, owner)) {
			done(peer);
		} else {
			AfterGift(session, owner, done);
		}
	}).fail([=] {
		AfterGift(session, owner, done);
	}).send();
}

} // namespace

void Resolve(
		not_null<Main::Session*> session,
		const Owner &owner,
		Fn<void(PeerData*)> done) {
	if (const auto peer = Loaded(session, owner)) {
		done(peer);
		return;
	} else if (!owner.known()) {
		done(nullptr);
		return;
	}
	const auto i = Failed.find(session);
	if (i != end(Failed) && i->second.contains(owner.peerId)) {
		done(nullptr);
		return;
	}
	if (Current().resolve == ResolveMode::ByUsername) {
		ByUsername(session, owner, done);
		return;
	} else if (owner.seeId.isEmpty()) {
		AfterGift(session, owner, done);
		return;
	}
	auto variables = QJsonObject();
	variables.insert(u"o"_q, owner.seeId);
	Api::Query(session, QString::fromLatin1(kQueryAnyNft), variables, [=](
			const QJsonObject &data) {
		const auto items = data.value(u"searchGifts"_q).toObject().value(
			u"items"_q).toArray();
		if (items.isEmpty()) {
			AfterGift(session, owner, done);
			return;
		}
		const auto first = items.first().toObject();
		const auto slug = first.value(u"slug"_q).toString()
			+ '-'
			+ QString::number(first.value(u"num"_q).toInt());
		ByGift(session, owner, slug, done);
	}, [=](const Api::Error &) {
		AfterGift(session, owner, done);
	});
}

bool Unknown(not_null<Main::Session*> session, PeerId id) {
	if (!id) {
		return false;
	}
	const auto peer = session->data().peerLoaded(id);
	return !peer || (peer->name().isEmpty() && peer->username().isEmpty());
}

namespace {

// The sender as see.tg knows them: their picture and their name, drawn the
// way the sheet draws a peer it does know. Not a link - the client has no
// key for this person yet, and that is what «Узнать кто» buys.
class UnknownPeer final : public Ui::RpWidget {
public:
	UnknownPeer(
		QWidget *parent,
		const style::FlatLabel &st,
		const QString &fallback);

	void setName(const QString &name);
	void setUserpicUrl(const QString &url);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void layout();

	not_null<Ui::FlatLabel*> _name;
	QImage _userpic;

};

UnknownPeer::UnknownPeer(
	QWidget *parent,
	const style::FlatLabel &st,
	const QString &fallback)
: RpWidget(parent)
, _name(Ui::CreateChild<Ui::FlatLabel>(this, fallback, st)) {
	resize(width(), st::giveawayGiftCodeUserpic.photoSize);
	_name->setAttribute(Qt::WA_TransparentForMouseEvents);
	widthValue() | rpl::on_next([=](int) {
		layout();
	}, lifetime());
	_name->naturalWidthValue() | rpl::on_next([=](int width) {
		setNaturalWidth(st::giveawayGiftCodeNamePosition.x() + width);
	}, _name->lifetime());
}

void UnknownPeer::layout() {
	const auto position = st::giveawayGiftCodeNamePosition;
	_name->resizeToNaturalWidth(width() - position.x());
	_name->moveToLeft(position.x(), position.y(), width());
}

void UnknownPeer::setName(const QString &name) {
	if (!name.isEmpty()) {
		_name->setText(name);
		layout();
	}
}

void UnknownPeer::setUserpicUrl(const QString &url) {
	Visuals::Image(url, crl::guard(this, [=](QImage image) {
		_userpic = std::move(image);
		update();
	}));
}

void UnknownPeer::paintEvent(QPaintEvent *e) {
	if (_userpic.isNull()) {
		return;
	}
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto size = st::giveawayGiftCodeUserpic.photoSize;
	const auto top = (height() - size) / 2;
	auto path = QPainterPath();
	path.addEllipse(QRect(0, top, size, size));
	p.setClipPath(path);
	p.drawImage(QRect(0, top, size, size), _userpic);
}

} // namespace

object_ptr<Ui::RpWidget> MakeUnknownSenderValue(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<ChatHelpers::Show> show,
		PeerId id) {
	const auto session = &show->session();
	if (!Unknown(session, id)) {
		return { nullptr };
	}
	const auto bare = QString::number(id.value & PeerId::kChatTypeMask);
	auto owner = Owner();
	owner.peerId = id;
	owner.seeId = SeeId(id);

	// The row holds see.tg's view of the person until the lookup succeeds,
	// then hands its place to the sheet's own peer row - so a resolved
	// sender looks exactly like one that never needed resolving.
	auto result = object_ptr<Ui::VerticalLayout>(table.get());
	const auto raw = result.data();
	const auto unknown = Ui::CreateChild<UnknownPeer>(
		raw,
		table->st().defaultValue,
		bare);
	const auto known = std::make_shared<Owner>(owner);
	auto value = Ui::MakeValueWithSmallButton(
		table,
		unknown,
		rpl::single(Lang::Text(Lang::Key::SeeTgWhoIs)),
		[=](not_null<Ui::RpWidget*> button) {
			button->setDisabled(true);
			Resolve(session, *known, crl::guard(raw, [=](PeerData *peer) {
				if (!peer) {
					button->setDisabled(false);
					show->showToast(Lang::Text(Lang::Key::SeeTgWhoIsFailed));
					return;
				}
				raw->clear();
				raw->add(Ui::MakePeerTableValue(table, show, id));
				raw->resizeToWidth(raw->width());
			}));
		});
	raw->add(std::move(value.widget));

	// Free of Telegram's daily ration: see.tg answers with the name and the
	// username, and the username is also the address of the picture.
	auto variables = QJsonObject();
	variables.insert(u"o"_q, owner.seeId);
	Api::Query(session, QString::fromLatin1(kQueryOwner), variables, crl::guard(
		unknown,
		[=](const QJsonObject &data) {
			auto found = ParseOwner(data.value(u"owner"_q).toObject());
			found.peerId = id;
			*known = found;
			unknown->setName(found.name);
			if (!found.username.isEmpty()) {
				unknown->setUserpicUrl(Visuals::UserpicUrl(found.username));
			}
		}), [](const Api::Error &) {
	});

	return result;
}

} // namespace Fork::SeeTg::Peers

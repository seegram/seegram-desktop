/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "fork/seetg/seetg_types.h"

// Turning a see.tg owner into a Telegram peer the client can show.
//
// see.tg knows telegram ids, but a peer the client has never met has no
// access hash, and without one Telegram answers nothing about it. Resolving
// by username is capped at a couple of hundred a day per account, so it is
// the last resort. The cheap way: ask see.tg for any one collectible the
// person holds and ask Telegram for that gift - the answer lists the holder
// with the hash. Only someone with no collectible and no username stays
// unknown.

#include "base/object_ptr.h"

class PeerData;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
class TableLayout;
} // namespace Ui

namespace Fork::SeeTg::Peers {

// Calls done with the loaded peer, or with nullptr when nothing worked.
// Runs synchronously when the peer is already known.
void Resolve(
	not_null<Main::Session*> session,
	const Owner &owner,
	Fn<void(PeerData*)> done);

// True while the client knows nothing about this peer but its id, which is
// how a gift opened from the see.tg tab arrives when looking senders up
// automatically is off.
[[nodiscard]] bool Unknown(
	not_null<Main::Session*> session,
	PeerId id);

// Hook, called from the gift sheet's sender row: the id, and a button that
// spends one lookup to turn it into a person. Null when the peer is known,
// so the sheet keeps its own row.
[[nodiscard]] object_ptr<Ui::RpWidget> MakeUnknownSenderValue(
	not_null<Ui::TableLayout*> table,
	std::shared_ptr<ChatHelpers::Show> show,
	PeerId id);

} // namespace Fork::SeeTg::Peers

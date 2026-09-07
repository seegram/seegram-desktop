#pragma once

#include <rpl/producer.h>

class PeerData;
namespace Main { class Session; }

namespace Fork::SeeTg::Counters {

enum class Kind { Transfers, Comments };

[[nodiscard]] rpl::producer<QString> Label(
	not_null<PeerData*> peer,
	Kind kind);
void Invalidate(not_null<Main::Session*> session, const QString &seeId);

} // namespace Fork::SeeTg::Counters

/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"
#include <rpl/producer.h>

namespace Main {
class Session;
} // namespace Main

namespace Fork::Stickers {

inline constexpr auto kDefaultRecentLimit = 50;
inline constexpr auto kMinimumRecentLimit = 20;

void Start();
[[nodiscard]] rpl::producer<> Changes();
[[nodiscard]] ::Settings::Type SectionId();
[[nodiscard]] int RecentDisplayLimit(
	not_null<Main::Session*> session,
	bool masks,
	bool upstreamUnlimited,
	int upstreamLimit);

} // namespace Fork::Stickers

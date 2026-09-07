/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>

namespace Fork {

inline constexpr char UpdateFeedPath[] = "/current4";
inline constexpr char UpdateFeedPrefix[] = "https://desktop.see.tg";

template <typename Checker>
[[nodiscard]] std::unique_ptr<Checker> TelegramUpdateChecker(
		bool canary,
		std::unique_ptr<Checker> checker) {
	if (!canary) {
		return nullptr;
	}
	return checker;
}

} // namespace Fork

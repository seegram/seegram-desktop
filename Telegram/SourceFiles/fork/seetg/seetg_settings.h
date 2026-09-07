/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

// The see.tg integration: the client talks to the see.tg GraphQL backend
// (the same one the see.tg mini app uses) and shows its data inside the
// client's own screens, starting with the gifts tab of a profile.
//
// On by default; the switch lives on the fork's settings page. Settings live
// in their own file, never in Core::Settings. See fork/RULES.md.

namespace Fork::SeeTg {

// How a see.tg owner becomes a peer the client can show. By gift asks
// Telegram for one collectible the person holds, which is not rationed; by
// username is capped at a couple of hundred lookups a day per account.
enum class ResolveMode {
	ByGift,
	ByUsername,
};

struct Settings {
	bool enabled = true;
	ResolveMode resolve = ResolveMode::ByGift;
	bool usernameFallback = true;
	bool resolveAutomatically = true;

	friend inline bool operator==(
		const Settings &,
		const Settings &) = default;
};

[[nodiscard]] const Settings &Current();
void Set(const Settings &settings);

// Called once from Core::Application before any session exists.
void Start();

[[nodiscard]] rpl::producer<Settings> Changes();
[[nodiscard]] rpl::producer<Settings> Value();

[[nodiscard]] bool Enabled();
[[nodiscard]] rpl::producer<bool> EnabledValue();

} // namespace Fork::SeeTg

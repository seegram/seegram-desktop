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

enum class Feature { Gifts, Transfers, Comments, Reactions, GiftDetails, MarketPreviews };

struct Settings {
	bool enabled = true;
	bool gifts = true;
	bool transfers = true;
	bool comments = true;
	bool reactions = true;
	bool giftDetails = true;
	bool marketPreviews = true;
	ResolveMode resolve = ResolveMode::ByGift;
	bool usernameFallback = true;
	bool resolveAutomatically = true;

	[[nodiscard]] bool featureEnabled(Feature feature) const {
		if (!enabled) return false;
		switch (feature) {
		case Feature::Gifts: return gifts;
		case Feature::Transfers: return transfers;
		case Feature::Comments: return comments;
		case Feature::Reactions: return reactions;
		case Feature::GiftDetails: return giftDetails;
		case Feature::MarketPreviews: return marketPreviews;
		}
		return false;
	}

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

[[nodiscard]] bool Enabled(Feature feature);
[[nodiscard]] rpl::producer<bool> EnabledValue(Feature feature);
[[nodiscard]] bool Enabled();
[[nodiscard]] rpl::producer<bool> EnabledValue();

} // namespace Fork::SeeTg

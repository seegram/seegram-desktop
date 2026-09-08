#pragma once

#include "fork/account_limits.h"

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

namespace Fork::AccountProfiles {

using Indices = std::set<int>;

struct Selection {
	std::optional<Indices> allowed;

	[[nodiscard]] bool master() const {
		return !allowed.has_value();
	}

	[[nodiscard]] bool permits(int index) const {
		return index >= 0
			&& index < Accounts::kNoLimit
			&& (!allowed || allowed->contains(index));
	}
};

[[nodiscard]] inline bool ValidIndices(const Indices &indices) {
	return std::all_of(begin(indices), end(indices), [](int index) {
		return index >= 0 && index < Accounts::kNoLimit;
	});
}

[[nodiscard]] inline std::vector<int> SelectAccounts(
		const std::vector<int> &stored,
		const Selection &selection) {
	auto result = std::vector<int>();
	auto seen = Indices();
	for (const auto index : stored) {
		if (selection.permits(index) && seen.emplace(index).second) {
			result.push_back(index);
		}
	}
	return result;
}

[[nodiscard]] inline std::optional<std::vector<int>> MergeStoredAccounts(
		const std::vector<int> &stored,
		const Indices &loaded,
		const std::vector<int> &current,
		const Selection &selection) {
	if (!ValidIndices(loaded)) {
		return std::nullopt;
	}
	for (const auto index : loaded) {
		if (!selection.permits(index)) {
			return std::nullopt;
		}
	}
	auto present = Indices();
	for (const auto index : current) {
		if (!selection.permits(index) || !present.emplace(index).second) {
			return std::nullopt;
		}
	}
	auto result = std::vector<int>();
	auto seen = Indices();
	for (const auto index : stored) {
		if (index < 0 || index >= Accounts::kNoLimit) {
			return std::nullopt;
		}
		if ((!loaded.contains(index) || present.contains(index))
			&& seen.emplace(index).second) {
			result.push_back(index);
		}
	}
	for (const auto index : current) {
		if (seen.emplace(index).second) {
			result.push_back(index);
		}
	}
	return result;
}

[[nodiscard]] inline std::optional<int> NextAccountIndex(
		const std::vector<int> &stored,
		const Indices &assigned) {
	if (!ValidIndices(assigned)) {
		return std::nullopt;
	}
	auto reserved = assigned;
	for (const auto index : stored) {
		if (index < 0 || index >= Accounts::kNoLimit) {
			return std::nullopt;
		}
		reserved.emplace(index);
	}
	auto index = 0;
	for (const auto used : reserved) {
		if (used != index) {
			break;
		}
		++index;
	}
	return (index < Accounts::kNoLimit)
		? std::optional(index)
		: std::nullopt;
}

} // namespace Fork::AccountProfiles

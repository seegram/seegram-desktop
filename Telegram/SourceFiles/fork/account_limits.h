#pragma once

#include <cstdint>
#include <limits>

namespace Fork::Accounts {

// No product or Premium quota. Keep the existing signed account IDs and
// leave room for the index + 1 used in account storage names.
inline constexpr auto kNoLimit = std::numeric_limits<int>::max() - 1;

[[nodiscard]] constexpr bool ValidStoredCount(
		std::int32_t count,
		std::int64_t remainingBytes) {
	// Bound parsing by the actual file, not the old six-account quota.
	return count > 0
		&& count <= kNoLimit
		&& remainingBytes >= std::int64_t(count) * std::int64_t(sizeof(std::int32_t));
}

} // namespace Fork::Accounts

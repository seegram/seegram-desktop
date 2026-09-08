#pragma once
#include <algorithm>
#include <vector>

namespace Fork::GiftBatch {
inline constexpr auto kMaximum = 9;
[[nodiscard]] constexpr int Maximum(int limitedCount, int left, int perUserTotal, int perUserLeft) {
	auto result = kMaximum;
	if (limitedCount > 0) result = std::min(result, left);
	if (perUserTotal > 0) result = std::min(result, perUserLeft);
	return std::max(0, result);
}
template <typename Slots>
[[nodiscard]] std::vector<int> ReverseFilledSlots(const Slots &slots) {
	std::vector<int> result;
	for (auto i = int(slots.size()); i > 0; --i) if (slots[i - 1]) result.push_back(i - 1);
	return result;
}
struct Progress {
	int total = 0;
	int maximum = kMaximum;
	int sent = 0;
	bool cancelled = false;
	bool finished = false;
	[[nodiscard]] bool canSend() const {
		return !finished && !cancelled && total > 0 && total <= maximum
			&& sent >= 0 && sent < total;
	}
};
}

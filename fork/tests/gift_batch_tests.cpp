#include "fork/gift_batch_policy.h"
#include <iostream>
#include <optional>
using namespace Fork::GiftBatch;
static_assert(Maximum(0, 0, 0, 0) == 9);
static_assert(Maximum(100, 4, 0, 0) == 4);
static_assert(Maximum(100, 4, 3, 2) == 2);
static_assert(Maximum(0, 0, 3, 0) == 0);
static_assert(Maximum(10, -1, 0, 0) == 0);
int main() {
 const auto slots = std::vector<std::optional<int>>{1,2,3,4,5,6,{}, {}, {}};
 if (ReverseFilledSlots(slots) != std::vector<int>{5,4,3,2,1,0}) return 6;
 const auto holes = std::vector<std::optional<int>>{1,{},3,{},5,{},7,{},9,10,{},12};
 if (ReverseFilledSlots(holes) != std::vector<int>{11,9,8,6,4,2,0}) return 7;
 auto grid = Progress{ .total = 12, .maximum = 12 };
 for (auto i = 0; i < 12; ++i) { if (!grid.canSend()) return 8; ++grid.sent; }
 if (grid.canSend()) return 9;
 grid.sent = 5; grid.cancelled = true;
 if (grid.canSend()) return 10;
 if (!ReverseFilledSlots(std::vector<std::optional<int>>(9)).empty()) return 11;

	for (auto count = 1; count <= 9; ++count) {
		auto state = Progress{ .total = count };
		for (auto i = 0; i < count; ++i) {
			if (!state.canSend()) return 1;
			++state.sent;
		}
		if (state.canSend()) return 2;
	}
	auto cancelled = Progress{ .total = 9, .sent = 2, .cancelled = true };
	++cancelled.sent; // A request already in flight may still be confirmed.
	if (cancelled.canSend() || cancelled.sent != 3) return 3;
	if (Progress{ .total = 9, .sent = 3, .finished = true }.canSend()) return 4;
	if (Progress{ .total = 10 }.canSend() || Progress{}.canSend()) return 5;
	std::cout << "Batch limits, stock, partial completion, cancellation and stop-on-error passed\n";
}

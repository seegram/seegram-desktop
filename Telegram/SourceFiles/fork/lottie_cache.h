#pragma once

#include <cstdint>

namespace Fork::LottieCache {

// lib_lottie cached transparent frames after RenderWork failures. Keep those
// entries separate from frames produced by the fixed renderer, including
// after the temporary tlottie patch has been replaced by an upstream fix.
// Current document/photo/thumbnail base keys leave the top byte unused.
[[nodiscard]] constexpr std::uint64_t High(
		std::uint64_t value,
		bool lottie = true) {
	return lottie ? (value ^ 0x5300000000000000ULL) : value;
}

} // namespace Fork::LottieCache

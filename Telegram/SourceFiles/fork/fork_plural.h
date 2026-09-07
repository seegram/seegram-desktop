#pragma once

#include <string_view>

namespace Fork::Lang {

enum class PluralForm { Zero, One, Two, Few, Many, Other };

[[nodiscard]] constexpr PluralForm IntegerPlural(
		std::string_view language,
		int count) {
	using enum PluralForm;
	const auto mod10 = count % 10;
	const auto mod100 = count % 100;
	if (language == "ru" || language == "uk") {
		if (mod10 == 1 && mod100 != 11) return One;
		if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return Few;
		return Many;
	} else if (language == "pl") {
		if (count == 1) return One;
		if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return Few;
		return Many;
	} else if (language == "cs") {
		return count == 1 ? One : (count >= 2 && count <= 4) ? Few : Other;
	} else if (language == "ro") {
		if (count == 1) return One;
		return (count == 0 || (mod100 >= 1 && mod100 <= 19)) ? Few : Other;
	} else if (language == "ar") {
		if (count == 0) return Zero;
		if (count == 1) return One;
		if (count == 2) return Two;
		if (mod100 >= 3 && mod100 <= 10) return Few;
		return mod100 >= 11 ? Many : Other;
	} else if (language == "he") {
		return count == 1 ? One : count == 2 ? Two : Other;
	} else if (language == "zh" || language == "ja" || language == "ko"
		|| language == "th" || language == "vi" || language == "id"
		|| language == "ms") {
		return Other;
	}
	const auto zeroIsOne = language == "fr" || language == "pt"
		|| language == "hi" || language == "bn" || language == "fa";
	if (count == 1 || (count == 0 && zeroIsOne)) return One;
	if (count != 0 && count % 1000000 == 0
		&& (language == "fr" || language == "pt"
			|| language == "es" || language == "it")) return Many;
	return Other;
}

} // namespace Fork::Lang

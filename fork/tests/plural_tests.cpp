#include "fork/fork_plural.h"

#include <cstdlib>
#include <iostream>

int main() {
	using enum Fork::Lang::PluralForm;
	struct Case {
		std::string_view language;
		int count;
		Fork::Lang::PluralForm expected;
	};
	const Case cases[] = {
		Case{ "ru", 0, Many }, { "ru", 1, One }, { "ru", 2, Few },
		{ "ru", 5, Many }, { "ru", 11, Many }, { "ru", 12, Many },
		{ "ru", 14, Many }, { "ru", 21, One }, { "ru", 22, Few },
		{ "ru", 25, Many }, { "ru", 101, One }, { "ru", 111, Many },
		{ "uk", 23, Few }, { "pl", 21, Many }, { "pl", 22, Few },
		{ "cs", 4, Few }, { "cs", 14, Other }, { "ro", 0, Few },
		{ "ro", 20, Other }, { "ro", 101, Few }, { "ar", 0, Zero },
		{ "ar", 1, One }, { "ar", 2, Two }, { "ar", 7, Few },
		{ "ar", 11, Many }, { "ar", 100, Other }, { "ar", 103, Few },
		{ "he", 2, Two }, { "he", 20, Other }, { "fr", 0, One },
		{ "fr", 1000000, Many }, { "pt", 0, One }, { "es", 0, Other },
		{ "en", 1, One }, { "en", 0, Other }, { "en", 11, Other },
		{ "hi", 0, One }, { "ja", 1, Other }, { "id", 1, Other },
	};
	for (const auto &test : cases) {
		if (Fork::Lang::IntegerPlural(test.language, test.count) != test.expected) {
			std::cerr << "Wrong form: " << test.language << ' ' << test.count << '\n';
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

#include "fork/seetg/seetg_settings.h"
#include <array>
#include <iostream>

int main() {
	using namespace Fork::SeeTg;
	const auto features = std::array{ Feature::Gifts, Feature::Transfers, Feature::Comments, Feature::Reactions, Feature::GiftDetails, Feature::MarketPreviews };
	for (const auto feature : features) {
		if (!Settings().featureEnabled(feature)) return 1;
	}
	for (auto mask = 0; mask != 128; ++mask) {
		auto settings = Settings();
		settings.enabled = mask & 64;
		settings.gifts = mask & 1;
		settings.transfers = mask & 2;
		settings.comments = mask & 4;
		settings.reactions = mask & 8;
		settings.giftDetails = mask & 16;
		settings.marketPreviews = mask & 32;
		for (auto i = 0; i != 6; ++i) {
			if (settings.featureEnabled(features[i]) != bool((mask & 64) && (mask & (1 << i)))) return 2;
		}
		settings.enabled = false;
		settings.enabled = true;
		for (auto i = 0; i != 6; ++i) {
			if (settings.featureEnabled(features[i]) != bool(mask & (1 << i))) return 3;
		}
	}
	std::cout << "Default-enabled features and all 128 combinations passed\n";
}

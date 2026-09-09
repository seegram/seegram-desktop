// Regression probe for gradient-rich TGS, using only tlottie's public C API.
// Generated data keeps this test independent of downloaded Telegram gifts.
#include <tlottie.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
	auto json = std::string(R"({"v":"5.5.0","w":64,"h":64,"fr":60,"ip":0,"op":3,"layers":[{"ty":4,"ind":0,"ip":0,"op":3,"st":0,"ks":{},"shapes":[{"ty":"gr","it":[{"ty":"rc","p":{"k":[32,32]},"s":{"k":[32,32]},"r":{"k":0}},)");
	for (auto i = 0; i != 280; ++i) {
		// Distinct animated gradients exercise cache misses on every frame.
		const auto stops = [&](int frame) {
			return "0," + std::to_string((i + frame + 1) / 300.)
				+ ",0,0,0.5,0,1,0,1,0,0,1,0,1,0.5,0.8,1,1";
		};
		json += R"({"ty":"gf","t":1,"s":{"k":[0,0]},"e":{"k":[64,64]},"g":{"p":3,"k":{"a":1,"k":[{"t":0,"s":[)"
			+ stops(0) + R"(],"h":1},{"t":1,"s":[)"
			+ stops(1) + R"(],"h":1},{"t":2,"s":[)"
			+ stops(2) + R"(]}]}},"o":{"k":100}},)";
	}
	json += R"({"ty":"tr"}]}]}]})";
	for (const auto size : { 64U, 192U }) {
		const auto instance = tlottie_new_with_options(
			reinterpret_cast<const uint8_t*>(json.data()), json.size(),
			TLOTTIE_FITZ_NONE, nullptr, 0, nullptr, 0, TLOTTIE_CHANNEL_BGRA);
		if (!instance) {
			std::cerr << "gradient fixture could not be parsed\n";
			return 1;
		}
		auto pixels = std::vector<uint32_t>(size * size);
		for (const auto frame : { 0, 1, 2, 0, 2 }) {
			const auto status = tlottie_render(instance, float(frame), size,
				size, pixels.data(), pixels.size(), 1);
			const auto visible = std::any_of(pixels.begin(), pixels.end(),
				[](uint32_t pixel) { return (pixel >> 24) != 0; });
			if (status != TLOTTIE_OK || !visible) {
				std::cerr << "gradient frame " << frame << " at " << size
					<< "px failed: " << status << '\n';
				tlottie_drop(instance);
				return 1;
			}
		}
		tlottie_drop(instance);
	}
	std::cout << "gradient regression passed\n";
}

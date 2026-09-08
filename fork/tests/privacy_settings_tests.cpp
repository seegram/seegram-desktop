#include "fork/ghost_mode.h"
#include "fork/spy_mode.h"
#include <iostream>

int main() {
	for (auto mask = 0; mask != 128; ++mask) {
		auto settings = Fork::Ghost::Settings{
			.blockReadReceipts = bool(mask & 1),
			.blockTyping = bool(mask & 2),
			.blockOnlineStatus = bool(mask & 4),
			.blockUploadProgress = bool(mask & 8),
			.blockStoryViews = bool(mask & 16),
			.useScheduledMessages = bool(mask & 32),
			.muteScheduledNotifications = bool(mask & 64),
		};
		if (settings.allEnabled() != ((mask & 31) == 31)
			|| settings.anyEnabled() != bool(mask & 31)) {
			return 1;
		}
		for (const auto enabled : { false, true }) {
			auto toggled = settings;
			toggled.setEnabled(enabled);
			if (toggled.muteScheduledNotifications != settings.muteScheduledNotifications
				|| toggled.useScheduledMessages != settings.useScheduledMessages
				|| toggled.blockStoryViews != enabled
				|| toggled.allEnabled() != enabled
				|| toggled.anyEnabled() != enabled) {
				return 2;
			}
		}
	}
	if (!Fork::Ghost::Settings().muteScheduledNotifications
		|| Fork::Ghost::Settings().useScheduledMessages
		|| !Fork::Spy::Settings().previewSelfDestructMedia) {
		return 3;
	}
	std::cout << "128 combinations and 256 master-toggle transitions passed\n";
}

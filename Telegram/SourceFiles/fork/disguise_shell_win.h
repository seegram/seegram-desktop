#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

namespace Fork::Disguise::Shell {

struct FileIdentity {
	std::uint32_t volume = 0;
	std::uint64_t index = 0;

	[[nodiscard]] explicit operator bool() const {
		return volume || index;
	}
	friend bool operator==(const FileIdentity &, const FileIdentity &) = default;
};

[[nodiscard]] FileIdentity IdentifyFile(const std::wstring &path);
bool UpdateShortcutIcon(
	const std::wstring &shortcut,
	const std::wstring &icon,
	FileIdentity executable);
[[nodiscard]] bool UpdateTaskbarIcon(
	HWND window,
	const std::wstring &appId,
	const std::wstring &icon);
void ClearTaskbarIcon(HWND window);

} // namespace Fork::Disguise::Shell

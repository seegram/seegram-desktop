#include "fork/disguise_shell_win.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <gsl/util>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

namespace {
namespace Shell = Fork::Disguise::Shell;
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

void Check(bool ok, const char *message) {
	if (!ok) throw std::runtime_error(message);
}

ComPtr<IShellLinkW> NewLink() {
	auto link = ComPtr<IShellLinkW>();
	Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf()))), "Create shell link");
	return link;
}

ComPtr<IPersistFile> FileInterface(const ComPtr<IShellLinkW> &link) {
	auto file = ComPtr<IPersistFile>();
	Check(SUCCEEDED(link.As(&file)), "Persist shell link");
	return file;
}

void CreateLink(const fs::path &path, const fs::path &target, const fs::path &icon) {
	const auto link = NewLink();
	Check(SUCCEEDED(link->SetPath(target.c_str())), "Set shortcut target");
	Check(SUCCEEDED(link->SetArguments(L"-many -workdir \"C:\\profile with spaces\"")), "Set arguments");
	Check(SUCCEEDED(link->SetWorkingDirectory(target.parent_path().c_str())), "Set working directory");
	Check(SUCCEEDED(link->SetDescription(L"User description")), "Set description");
	Check(SUCCEEDED(link->SetIconLocation(icon.c_str(), 0)), "Set original icon");
	Check(SUCCEEDED(FileInterface(link)->Save(path.c_str(), TRUE)), "Save shell link");
}

void VerifyLink(const fs::path &path, const fs::path &target, const fs::path &icon) {
	const auto link = NewLink();
	Check(SUCCEEDED(FileInterface(link)->Load(path.c_str(), STGM_READ)), "Reload shortcut");
	auto text = std::array<wchar_t, 32768>();
	auto index = 0;
	Check(SUCCEEDED(link->GetIconLocation(text.data(), int(text.size()), &index))
		&& !index && icon == text.data(), "Selected icon persists on shortcut");
	Check(SUCCEEDED(link->GetPath(text.data(), int(text.size()), nullptr, 0))
		&& target == text.data(), "Shortcut target is preserved");
	Check(SUCCEEDED(link->GetArguments(text.data(), int(text.size())))
		&& std::wstring(text.data()) == L"-many -workdir \"C:\\profile with spaces\"", "Shortcut arguments are preserved");
	Check(SUCCEEDED(link->GetWorkingDirectory(text.data(), int(text.size())))
		&& target.parent_path() == text.data(), "Working directory is preserved");
	Check(SUCCEEDED(link->GetDescription(text.data(), int(text.size())))
		&& std::wstring(text.data()) == L"User description", "Description is preserved");
}

void CheckProperty(HWND window, const PROPERTYKEY &key, const std::wstring &expected) {
	auto store = ComPtr<IPropertyStore>();
	Check(SUCCEEDED(SHGetPropertyStoreForWindow(window,
		IID_PPV_ARGS(store.GetAddressOf()))), "Read window property store");
	auto value = PROPVARIANT();
	const auto clear = gsl::finally([&] { PropVariantClear(&value); });
	Check(SUCCEEDED(store->GetValue(key, &value)), "Read window property");
	Check(expected.empty() ? value.vt == VT_EMPTY
		: value.vt == VT_LPWSTR && expected == value.pwszVal, "Window property matches selection");
}

void CheckIcons(const fs::path &path) {
	for (const auto size : { 16, 24, 32, 48, 64, 128, 256 }) {
		const auto icon = static_cast<HICON>(LoadImageW(nullptr, path.c_str(),
			IMAGE_ICON, size, size, LR_LOADFROMFILE));
		Check(icon != nullptr, "Windows decodes bundled ICO at requested size");
		const auto destroy = gsl::finally([&] { DestroyIcon(icon); });
		auto info = ICONINFO();
		Check(GetIconInfo(icon, &info), "Read native icon bitmap");
		const auto release = gsl::finally([&] {
			DeleteObject(info.hbmColor);
			DeleteObject(info.hbmMask);
		});
		auto bitmap = BITMAP();
		Check(GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap)
			&& bitmap.bmWidth == size && bitmap.bmHeight == size,
			"Native icon keeps requested dimensions");
	}
}

void Run(const fs::path &seegram, const fs::path &telegram) {
	CheckIcons(seegram);
	CheckIcons(telegram);
	std::cout << "PASS: Windows decodes both ICO files at all seven sizes\n";

	auto temp = std::array<wchar_t, MAX_PATH>();
	Check(GetTempFileNameW(fs::temp_directory_path().c_str(), L"sgi", 0, temp.data()), "Create disposable test path");
	const auto directory = fs::path(temp.data());
	fs::remove(directory);
	fs::create_directory(directory);
	const auto remove = gsl::finally([&] { fs::remove_all(directory); });
	const auto target = directory / L"Client with spaces.exe";
	const auto other = directory / L"Other client.exe";
	std::ofstream(target) << "test fixture";
	std::ofstream(other) << "other fixture";
	const auto shortcut = directory / L"Renamed shortcut.lnk";
	const auto foreign = directory / L"SeeGram.lnk";
	CreateLink(shortcut, target, seegram);
	CreateLink(foreign, other, seegram);
	const auto identity = Shell::IdentifyFile(target.wstring());
	Check(bool(identity), "Identify current executable");
	Check(Shell::UpdateShortcutIcon(shortcut.wstring(), telegram.wstring(), identity), "Apply Telegram icon");
	VerifyLink(shortcut, target, telegram);
	Check(Shell::UpdateShortcutIcon(shortcut.wstring(), seegram.wstring(), identity), "Restore SeeGram icon");
	VerifyLink(shortcut, target, seegram);
	Check(Shell::UpdateShortcutIcon(shortcut.wstring(), seegram.wstring(), identity), "Reapply current icon to refresh shell cache");
	Check(!Shell::UpdateShortcutIcon(foreign.wstring(), telegram.wstring(), identity), "Do not change another client's shortcut");
	VerifyLink(foreign, other, seegram);
	Check(!Shell::UpdateShortcutIcon(shortcut.wstring(), telegram.wstring(), {}), "Reject missing executable identity");
	std::cout << "PASS: renamed shortcuts switch icons, preserve launch settings and skip foreign targets\n";

	const auto window = CreateWindowExW(0, L"STATIC", L"Disguise test",
		WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	Check(window != nullptr, "Create disposable hidden window");
	const auto close = gsl::finally([&] {
		Shell::ClearTaskbarIcon(window);
		DestroyWindow(window);
	});
	const auto appId = std::wstring(L"SeeGram.Icon.Test");
	for (const auto &icon : { seegram, telegram, seegram }) {
		Check(Shell::UpdateTaskbarIcon(window, appId, icon.wstring()), "Apply native taskbar properties");
		CheckProperty(window, PKEY_AppUserModel_ID, appId);
		CheckProperty(window, PKEY_AppUserModel_RelaunchIconResource, icon.wstring() + L",0");
	}
	Shell::ClearTaskbarIcon(window);
	CheckProperty(window, PKEY_AppUserModel_ID, {});
	CheckProperty(window, PKEY_AppUserModel_RelaunchIconResource, {});
	std::cout << "PASS: native window properties switch both ways and are released on close\n";
}
} // namespace

int wmain(int argc, wchar_t **argv) {
	try {
		Check(argc == 3, "Expected both bundled ICO paths");
		Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "Initialize COM");
		const auto cleanup = gsl::finally([] { CoUninitialize(); });
		Run(fs::absolute(argv[1]), fs::absolute(argv[2]));
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}

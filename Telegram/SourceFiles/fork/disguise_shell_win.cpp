// Windows icon handling follows AyuGram Desktop by Radolyn (2026).
// Source references and GPL-3.0 attribution: fork/account-profiles.md.
#include "fork/disguise_shell_win.h"

#include <array>
#include <gsl/util>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

namespace Fork::Disguise::Shell {
namespace {

bool SetProperty(
		IPropertyStore *store,
		const PROPERTYKEY &key,
		const std::wstring &text) {
	auto value = PROPVARIANT();
	if (FAILED(InitPropVariantFromString(text.c_str(), &value))) {
		return false;
	}
	const auto guard = gsl::finally([&] { PropVariantClear(&value); });
	return SUCCEEDED(store->SetValue(key, value));
}

} // namespace

FileIdentity IdentifyFile(const std::wstring &path) {
	const auto file = CreateFileW(path.c_str(), 0,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return {};
	}
	const auto guard = gsl::finally([&] { CloseHandle(file); });
	auto info = BY_HANDLE_FILE_INFORMATION();
	const auto result = GetFileInformationByHandle(file, &info);
	return result ? FileIdentity{
		info.dwVolumeSerialNumber,
		(std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow,
	} : FileIdentity();
}

bool UpdateShortcutIcon(
		const std::wstring &shortcut,
		const std::wstring &icon,
		FileIdentity executable) {
	if (!executable) {
		return false;
	}
	auto link = Microsoft::WRL::ComPtr<IShellLinkW>();
	if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(link.GetAddressOf())))) {
		return false;
	}
	auto file = Microsoft::WRL::ComPtr<IPersistFile>();
	if (FAILED(link.As(&file))
		|| FAILED(file->Load(shortcut.c_str(), STGM_READWRITE))) {
		return false;
	}
	auto target = std::array<wchar_t, 32768>();
	if (FAILED(link->GetPath(target.data(), int(target.size()), nullptr, 0))
		|| IdentifyFile(target.data()) != executable) {
		return false;
	}
	if (FAILED(link->SetIconLocation(icon.c_str(), 0))
		|| FAILED(file->Save(shortcut.c_str(), TRUE))) {
		return false;
	}
	SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT,
		shortcut.c_str(), nullptr);
	return true;
}

bool UpdateTaskbarIcon(
		HWND window,
		const std::wstring &appId,
		const std::wstring &icon) {
	auto store = Microsoft::WRL::ComPtr<IPropertyStore>();
	return SUCCEEDED(SHGetPropertyStoreForWindow(window,
		IID_PPV_ARGS(store.GetAddressOf())))
		&& SetProperty(store.Get(), PKEY_AppUserModel_ID, appId)
		&& SetProperty(store.Get(), PKEY_AppUserModel_RelaunchIconResource,
			icon + L",0");
}

void ClearTaskbarIcon(HWND window) {
	if (!window) {
		return;
	}
	auto store = Microsoft::WRL::ComPtr<IPropertyStore>();
	if (SUCCEEDED(SHGetPropertyStoreForWindow(window,
		IID_PPV_ARGS(store.GetAddressOf())))) {
		const auto empty = PROPVARIANT();
		store->SetValue(PKEY_AppUserModel_RelaunchIconResource, empty);
		store->SetValue(PKEY_AppUserModel_ID, empty);
	}
}

} // namespace Fork::Disguise::Shell

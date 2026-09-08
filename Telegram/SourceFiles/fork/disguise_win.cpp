#include "fork/disguise.h"

#include "base/platform/win/base_windows_winrt.h"
#include "platform/win/windows_app_user_model_id.h"
#include "window/main_window.h"

#include <crl/crl_async.h>
#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDirIterator>
#include <QtGui/QImage>
#include <array>
#include <atomic>
#include <mutex>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>

namespace Platform {
void WriteIco(const QString &path, std::vector<QImage> images);
} // namespace Platform

namespace Fork::Disguise {
namespace {

struct ShortcutState {
	std::atomic<uint64> revision = 0;
	std::mutex mutex;
};

const auto Shortcuts = std::make_shared<ShortcutState>();

QString ShellIconPath() {
	static auto cachedKey = qint64(0);
	static auto cachedPath = QString();
	const auto &image = Image();
	if (image.isNull()) {
		return {};
	} else if (cachedKey == image.cacheKey() && QFile::exists(cachedPath)) {
		return cachedPath;
	}
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	if (!image.save(&buffer, "PNG")) {
		return {};
	}
	const auto hash = QCryptographicHash::hash(
		bytes, QCryptographicHash::Sha256).toHex();
	const auto path = cWorkingDir() + u"tdata/icons/"_q
		+ QString::fromLatin1(hash) + u".ico"_q;
	if (!QFile::exists(path)) {
		auto images = std::vector<QImage>();
		for (const auto size : { 16, 24, 32, 48, 64, 128, 256 }) {
			images.push_back(image.scaled(size, size,
				Qt::KeepAspectRatio, Qt::SmoothTransformation));
		}
		const auto staging = path + u".new"_q;
		Platform::WriteIco(staging, std::move(images));
		if (!QFileInfo(staging).size() || !QFile::rename(staging, path)) {
			QFile::remove(staging);
			return {};
		}
	}
	cachedKey = image.cacheKey();
	cachedPath = path;
	return path;
}

QString KnownFolder(REFKNOWNFOLDERID id) {
	auto path = PWSTR();
	const auto result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path);
	const auto guard = gsl::finally([&] { CoTaskMemFree(path); });
	return SUCCEEDED(result) ? QString::fromWCharArray(path) : QString();
}

bool SetProperty(
		not_null<IPropertyStore*> store,
		const PROPERTYKEY &key,
		const std::wstring &text) {
	auto value = PROPVARIANT();
	if (FAILED(InitPropVariantFromString(text.c_str(), &value))) {
		return false;
	}
	const auto guard = gsl::finally([&] { PropVariantClear(&value); });
	return SUCCEEDED(store->SetValue(key, value));
}

bool UpdateShortcut(
		const QString &path,
		const std::wstring &icon,
		Platform::AppUserModelId::UniqueFileId executableId) {
	const auto link = base::WinRT::TryCreateInstance<IShellLinkW>(CLSID_ShellLink);
	if (!link) {
		return false;
	}
	const auto file = link.try_as<IPersistFile>();
	const auto nativePath = QDir::toNativeSeparators(path).toStdWString();
	if (!file || FAILED(file->Load(nativePath.c_str(), STGM_READWRITE))) {
		return false;
	}
	auto target = std::array<wchar_t, 32768>();
	if (FAILED(link->GetPath(target.data(), int(target.size()), nullptr, SLGP_RAWPATH))
		|| Platform::AppUserModelId::GetUniqueFileId(target.data()) != executableId) {
		return false;
	}
	auto current = std::array<wchar_t, 32768>();
	auto index = 0;
	if (SUCCEEDED(link->GetIconLocation(current.data(), int(current.size()), &index))
		&& !index && icon == current.data()) {
		return false;
	}
	if (FAILED(link->SetIconLocation(icon.c_str(), 0))
		|| FAILED(file->Save(nativePath.c_str(), TRUE))) {
		return false;
	}
	SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT,
		nativePath.c_str(), nullptr);
	return true;
}

void UpdateShortcuts(
		const QString &iconPath,
		Platform::AppUserModelId::UniqueFileId executableId,
		const std::shared_ptr<ShortcutState> &state,
		uint64 revision) {
	const auto lock = std::lock_guard(state->mutex);
	if (state->revision != revision || !executableId
		|| FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
		return;
	}
	const auto guard = gsl::finally([] { CoUninitialize(); });
	const auto icon = QDir::toNativeSeparators(iconPath).toStdWString();
	auto roots = QStringList();
	for (const auto id : { &FOLDERID_Desktop, &FOLDERID_PublicDesktop,
		&FOLDERID_Programs, &FOLDERID_CommonPrograms }) {
		const auto path = KnownFolder(*id);
		if (!path.isEmpty()) {
			roots.push_back(path);
		}
	}
	const auto appData = KnownFolder(FOLDERID_RoamingAppData);
	if (!appData.isEmpty()) {
		roots.push_back(appData
			+ u"/Microsoft/Internet Explorer/Quick Launch/User Pinned/TaskBar"_q);
	}
	auto changed = false;
	for (const auto &root : roots) {
		auto entries = QDirIterator(root, { u"*.lnk"_q }, QDir::Files,
			QDirIterator::Subdirectories);
		while (entries.hasNext()) {
			if (state->revision != revision) {
				return;
			}
			changed = UpdateShortcut(entries.next(), icon, executableId) || changed;
		}
	}
	if (changed) {
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT,
			nullptr, nullptr);
	}
}

} // namespace

void RefreshNativeIcon(not_null<Window::MainWindow*> window) {
	if (!Generation()) {
		return;
	}
	const auto path = ShellIconPath();
	if (path.isEmpty()) {
		return;
	}
	const auto hwnd = reinterpret_cast<HWND>(window->winId());
	auto store = winrt::com_ptr<IPropertyStore>();
	if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(store.put())))) {
		if (SetProperty(store.get(), PKEY_AppUserModel_RelaunchIconResource,
				QDir::toNativeSeparators(path).toStdWString() + L",0")
			&& SetProperty(store.get(), PKEY_AppUserModel_ID,
				Platform::AppUserModelId::Id())) {
			store->Commit();
		}
	}
	static auto previous = QString();
	if (previous == path) {
		return;
	}
	previous = path;
	const auto state = Shortcuts;
	const auto revision = ++state->revision;
	const auto executableId = Platform::AppUserModelId::MyExecutablePathId();
	crl::async([=] { UpdateShortcuts(path, executableId, state, revision); });
}

} // namespace Fork::Disguise

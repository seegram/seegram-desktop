#include "fork/disguise.h"

#include "fork/disguise_shell_win.h"
#include "platform/win/windows_app_user_model_id.h"
#include "window/main_window.h"

#include <crl/crl_async.h>
#include <QtCore/QDirIterator>
#include <QtCore/QSaveFile>
#include <atomic>
#include <mutex>
#include <shlobj.h>

namespace Fork::Disguise {
namespace {

struct ShortcutState {
	std::atomic<uint64> revision = 0;
	std::mutex mutex;
};

const auto Shortcuts = std::make_shared<ShortcutState>();

QString ShellIconPath() {
	static auto prepared = std::optional<Icon>();
	static auto cachedPath = QString();
	const auto choice = AppChoice();
	if (prepared == choice && QFile::exists(cachedPath)) {
		return cachedPath;
	}
	const auto name = choice == Icon::Telegram ? u"telegram"_q : u"seegram"_q;
	auto resource = QFile(u":/seegram/"_q + name + u".ico"_q);
	if (!resource.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto bytes = resource.readAll();
	if (bytes.isEmpty()) {
		return {};
	}
	const auto path = cWorkingDir() + u"tdata/SeeGram-"_q + name + u".ico"_q;
	auto existing = QFile(path);
	const auto matches = existing.open(QIODevice::ReadOnly)
		&& existing.readAll() == bytes;
	existing.close();
	if (!matches) {
		auto output = QSaveFile(path);
		if (!QDir().mkpath(QFileInfo(path).absolutePath())
			|| !output.open(QIODevice::WriteOnly)
			|| output.write(bytes) != bytes.size()
			|| !output.commit()) {
			return {};
		}
	}
	prepared = choice;
	cachedPath = path;
	return path;
}

QString KnownFolder(REFKNOWNFOLDERID id) {
	auto path = PWSTR();
	const auto result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path);
	const auto guard = gsl::finally([&] { CoTaskMemFree(path); });
	return SUCCEEDED(result) ? QString::fromWCharArray(path) : QString();
}

void UpdateShortcuts(
		const QString &iconPath,
		Shell::FileIdentity executableId,
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
	for (const auto &root : roots) {
		auto entries = QDirIterator(root, { u"*.lnk"_q }, QDir::Files,
			QDirIterator::Subdirectories);
		while (entries.hasNext()) {
			if (state->revision != revision) {
				return;
			}
			const auto path = QDir::toNativeSeparators(entries.next()).toStdWString();
			Shell::UpdateShortcutIcon(path, icon, executableId);
		}
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT,
		nullptr, nullptr);
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
	if (!Shell::UpdateTaskbarIcon(hwnd, Platform::AppUserModelId::Id(),
		QDir::toNativeSeparators(path).toStdWString())) {
		return;
	}
	static auto previous = uint64(0);
	if (previous == Generation()) {
		return;
	}
	previous = Generation();
	const auto state = Shortcuts;
	const auto revision = ++state->revision;
	const auto executableId = Shell::IdentifyFile(
		Platform::AppUserModelId::MyExecutablePath());
	crl::async([=] { UpdateShortcuts(path, executableId, state, revision); });
}

void ClearNativeIcon(not_null<Window::MainWindow*> window) {
	Shell::ClearTaskbarIcon(reinterpret_cast<HWND>(window->internalWinId()));
}

} // namespace Fork::Disguise

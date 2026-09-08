#include "storage/storage_domain.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "data/data_user.h"
#include "storage/storage_account.h"
#include "storage/serialize_peer.h"
#include "core/version.h"
#include "mtproto/mtproto_config.h"
#include "base/random.h"
#include "core/application.h"
#include "calls/calls_instance.h"
#include "window/window_controller.h"
#include "window/notifications_manager.h"

namespace Storage {
namespace {

using namespace details;
constexpr auto kMaxProfiles = 16;

} // namespace

QByteArray Domain::decryptProfileKey(
		const QByteArray &passcode,
		const QByteArray &salt,
		const QByteArray &encrypted,
		MTP::AuthKeyPtr &key) const {
	if (passcode.isEmpty() || salt.size() != LocalEncryptSaltSize
		|| encrypted.isEmpty() || encrypted.size() > 1024) {
		return {};
	}
	auto data = EncryptedDescriptor();
	if (!DecryptLocal(data, encrypted, CreateLocalKey(passcode, salt))) {
		return {};
	}
	const auto raw = Serialize::read<MTP::AuthKey::Data>(data.stream);
	auto id = QByteArray();
	data.stream >> id;
	if (data.stream.status() != QDataStream::Ok
		|| !data.stream.atEnd() || id.size() != 16) {
		return {};
	}
	key = std::make_shared<MTP::AuthKey>(raw);
	return id;
}

Fork::AccountProfiles::Selection Domain::profileSelection(
		const QByteArray &id) const {
	if (id.isEmpty()) {
		return {};
	}
	for (const auto &profile : _accountProfiles) {
		if (profile.id == id) {
			return { profile.accounts };
		}
	}
	return { Fork::AccountProfiles::Indices() };
}

bool Domain::restrictedProfile() const {
	return !_activeProfile.isEmpty();
}

bool Domain::hasAccountProfiles() const {
	return !_accountProfiles.empty();
}

bool Domain::cleanProfile() const {
	for (const auto &profile : _accountProfiles) {
		if (profile.id == _activeProfile) {
			return profile.clean;
		}
	}
	return false;
}

Fork::Disguise::Settings Domain::disguiseSettings() const {
	return restrictedProfile() ? Fork::Disguise::Settings() : _disguise;
}

bool Domain::setDisguiseSettings(const Fork::Disguise::Settings &settings) {
	if (restrictedProfile() || !Fork::Disguise::ValidName(settings.name)
		|| settings.trayIcon < Fork::Disguise::TrayIcon::Application
		|| settings.trayIcon > Fork::Disguise::TrayIcon::Telegram
		|| (settings.icon != Fork::Disguise::Icon::SeeGram
			&& settings.icon != Fork::Disguise::Icon::Telegram)) {
		return false;
	}
	_disguise = settings;
	_disguise.name = _disguise.name.trimmed();
	writeAccounts();
	applyDisguise();
	Fork::Disguise::RefreshApplication();
	return true;
}

void Domain::applyDisguise() const {
	Fork::Disguise::Apply(cleanProfile(), _disguise);
}

bool Domain::readDisguise(QDataStream &stream) {
	_disguise = {};
	if (stream.atEnd()) {
		return true;
	}
	auto magic = quint32();
	auto icon = quint32();
	auto count = quint32();
	stream >> magic >> _disguise.name >> icon >> count;
	if (magic != 0x53474D31 || icon > 1 || count > _accountProfiles.size()
		|| !Fork::Disguise::ValidName(_disguise.name)) {
		return false;
	}
	_disguise.icon = Fork::Disguise::Icon(icon);
	auto seen = std::set<QByteArray>();
	for (auto i = 0U; i != count; ++i) {
		auto id = QByteArray();
		stream >> id;
		const auto profile = ranges::find(_accountProfiles, id, &AccountProfile::id);
		if (profile == end(_accountProfiles) || !seen.emplace(id).second) {
			return false;
		}
		profile->clean = true;
	}
	if (!stream.atEnd()) {
		auto tray = quint32();
		stream >> tray;
		if (tray > 2) return false;
		_disguise.trayIcon = Fork::Disguise::TrayIcon(tray);
	}
	return stream.status() == QDataStream::Ok && stream.atEnd();
}

void Domain::writeDisguise(QDataStream &stream) const {
	const auto count = ranges::count_if(_accountProfiles, &AccountProfile::clean);
	stream << quint32(0x53474D31) << _disguise.name
		<< quint32(_disguise.icon) << quint32(count);
	for (const auto &profile : _accountProfiles) {
		if (profile.clean) {
			stream << profile.id;
		}
	}
	stream << quint32(_disguise.trayIcon);
}

const std::vector<Domain::AccountProfile> &Domain::accountProfiles() const {
	static const auto empty = std::vector<AccountProfile>();
	return restrictedProfile() ? empty : _accountProfiles;
}

bool Domain::tryUnlockPasscode(const QByteArray &passcode) {
	_pendingProfile.reset();
	if (checkPasscode(passcode)) {
		_pendingProfile = QByteArray();
		return true;
	}
	for (const auto &profile : _accountProfiles) {
		auto key = MTP::AuthKeyPtr();
		const auto id = decryptProfileKey(
			passcode, profile.salt, profile.encryptedKey, key);
		if (id == profile.id && key && key->equals(_localKey)) {
			const auto selected = Fork::AccountProfiles::SelectAccounts(
				_storedAccounts, profileSelection(id));
			if (!selected.empty()) {
				_pendingProfile = id;
				return true;
			}
		}
	}
	return false;
}

bool Domain::applyPendingProfile() {
	if (!_pendingProfile) {
		return true;
	}
	const auto target = *base::take(_pendingProfile);
	if (target == _activeProfile) {
		return true;
	}
	writeAccounts();
	const auto selected = Fork::AccountProfiles::SelectAccounts(
		_storedAccounts, profileSelection(target));
	if (selected.empty()) {
		return false;
	}
	const auto wasClean = cleanProfile();
	_activeProfile = target;
	const auto reload = (wasClean != cleanProfile());
	applyDisguise();
	auto added = std::vector<Main::Domain::AccountWithIndex>();
	for (const auto index : selected) {
		if (_loadedAccounts.contains(index)) {
			continue;
		}
		auto account = std::make_unique<Main::Account>(_owner, _dataName, index);
		auto config = account->prepareToStart(_localKey);
		account->start(std::move(config));
		added.push_back({ index, std::move(account) });
	}
	_owner->applyAccountProfile(selected, std::move(added), reload);
	_loadedAccounts = Fork::AccountProfiles::Indices(
		begin(selected), end(selected));
	writeAccounts();
	Fork::Disguise::RefreshApplication();
	return true;
}

bool Domain::saveAccountProfile(
		const QByteArray &id,
		const QString &name,
		const Fork::AccountProfiles::Indices &accounts,
		const QByteArray &passcode,
		std::optional<bool> clean) {
	if (restrictedProfile() || !hasLocalPasscode()
		|| name.trimmed().isEmpty() || name.size() > 128
		|| accounts.empty() || !Fork::AccountProfiles::ValidIndices(accounts)) {
		return false;
	}
	for (const auto index : accounts) {
		if (!ranges::contains(_owner->accounts(), index,
				&Main::Domain::AccountWithIndex::index)) {
			return false;
		}
	}
	auto existing = ranges::find(_accountProfiles, id, &AccountProfile::id);
	if (!id.isEmpty() && existing == end(_accountProfiles)) {
		return false;
	}
	if (id.isEmpty() && (_accountProfiles.size() >= kMaxProfiles
		|| passcode.isEmpty())) {
		return false;
	}
	if (!passcode.isEmpty()) {
		if (checkPasscode(passcode)) {
			return false;
		}
		for (const auto &other : _accountProfiles) {
			auto key = MTP::AuthKeyPtr();
			if (other.id != id && !decryptProfileKey(
					passcode, other.salt, other.encryptedKey, key).isEmpty()) {
				return false;
			}
		}
	}
	auto profile = (existing != end(_accountProfiles))
		? *existing : AccountProfile();
	if (profile.id.isEmpty()) {
		profile.id.resize(16);
		base::RandomFill(profile.id.data(), profile.id.size());
	}
	profile.name = name.trimmed();
	profile.accounts = accounts;
	if (clean) {
		profile.clean = *clean;
	}
	if (!passcode.isEmpty()) {
		profile.salt.resize(LocalEncryptSaltSize);
		base::RandomFill(profile.salt.data(), profile.salt.size());
		auto data = EncryptedDescriptor(0);
		_localKey->write(data.stream);
		data.stream << profile.id;
		profile.encryptedKey = PrepareEncrypted(data,
			CreateLocalKey(passcode, profile.salt));
	}
	if (existing == end(_accountProfiles)) {
		_accountProfiles.push_back(std::move(profile));
	} else {
		*existing = std::move(profile);
	}
	writeAccounts();
	_accountProfilesChanged.fire({});
	return true;
}

bool Domain::removeAccountProfile(const QByteArray &id) {
	if (restrictedProfile()) {
		return false;
	}
	const auto i = ranges::find(_accountProfiles, id, &AccountProfile::id);
	if (i == end(_accountProfiles)) {
		return false;
	}
	_accountProfiles.erase(i);
	writeAccounts();
	_accountProfilesChanged.fire({});
	return true;
}

bool Domain::accountIndexReserved(int index) const {
	if (ranges::contains(_storedAccounts, index)) {
		return true;
	}
	for (const auto &profile : _accountProfiles) {
		if (profile.accounts.contains(index)) {
			return true;
		}
	}
	return false;
}

rpl::producer<> Domain::accountProfilesChanged() const {
	return _accountProfilesChanged.events();
}

} // namespace Storage

namespace Main {

void Account::reloadSessionForProfile() {
	const auto session = maybeSession();
	if (!session) {
		return;
	}
	session->saveSettingsNowIfNeeded();
	local().writeSearchSuggestionsIfNeeded();
	auto settings = std::make_unique<SessionSettings>();
	settings->addFromSerialized(session->settings().serialize());
	const auto self = session->user();
	const auto id = peerToUser(self->id);
	auto serialized = QByteArray();
	{
		auto stream = QDataStream(&serialized, QIODevice::WriteOnly);
		Serialize::writePeer(stream, self);
		stream << self->about();
	}
	destroySession(DestroyReason::Quitting);
	_sessionUserId = id;
	createSession(id, std::move(serialized), AppVersion, std::move(settings));
}

void Domain::applyAccountProfile(
		const std::vector<int> &selected,
		std::vector<AccountWithIndex> added,
		bool reloadSessions) {
	Expects(!selected.empty());
	_switchingProfiles = true;
	Core::App().calls().endForAccountSwitch();
	Core::App().notifications().clearAll();
	const auto keep = Core::App().activePrimaryWindow();
	Expects(keep != nullptr);
	auto close = std::vector<not_null<Window::Controller*>>();
	Core::App().enumerateWindows([&](not_null<Window::Controller*> window) {
		if (window != keep) {
			close.push_back(window);
		}
	});
	for (const auto window : close) {
		Core::App().closeWindow(window);
	}
	keep->hideSettingsAndLayer(anim::type::instant);
	if (reloadSessions) {
		for (const auto &entry : _accounts) {
			if (ranges::contains(selected, entry.index)) {
				entry.account->reloadSessionForProfile();
			}
		}
	}
	for (auto &entry : added) {
		const auto account = entry.account.get();
		accountAddedInStorage(std::move(entry));
		watchSession(account);
	}
	const auto targetIndex = ranges::contains(selected, _accountToActivate)
		? _accountToActivate : selected.front();
	const auto target = ranges::find(_accounts, targetIndex,
		&AccountWithIndex::index)->account.get();
	activate(target);
	keep->showAccount(target);

	auto removed = std::vector<AccountWithIndex>();
	for (auto i = begin(_accounts); i != end(_accounts);) {
		if (!ranges::contains(selected, i->index)) {
			removed.push_back(std::move(*i));
			i = _accounts.erase(i);
		} else {
			++i;
		}
	}
	_lastActiveIndex = -1;
	_accountsChanges.fire({});
	removed.clear();
	updateUnreadBadge();
	_switchingProfiles = false;
}

} // namespace Main

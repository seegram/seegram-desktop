#include "storage/storage_domain.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "main/main_domain.h"
#include "main/main_account.h"
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
	_activeProfile = target;
	_owner->applyAccountProfile(selected, std::move(added));
	_loadedAccounts = Fork::AccountProfiles::Indices(
		begin(selected), end(selected));
	writeAccounts();
	return true;
}

bool Domain::saveAccountProfile(
		const QByteArray &id,
		const QString &name,
		const Fork::AccountProfiles::Indices &accounts,
		const QByteArray &passcode) {
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

void Domain::applyAccountProfile(
		const std::vector<int> &selected,
		std::vector<AccountWithIndex> added) {
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

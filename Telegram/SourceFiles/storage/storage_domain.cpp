/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_domain.h"
#include "fork/account_limits.h"

#include "core/version.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "mtproto/mtproto_config.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "base/random.h"

namespace Storage {
namespace {

using namespace details;

constexpr auto kProfilesMagic = quint32(0x53475031);
constexpr auto kMaxProfiles = 16;

[[nodiscard]] QString BaseGlobalPath() {
	return cWorkingDir() + u"tdata/"_q;
}

[[nodiscard]] QString ComputeKeyName(const QString &dataName) {
	// We dropped old test authorizations when migrated to multi auth.
	//return "key_" + dataName + (cTestMode() ? "[test]" : "");
	return "key_" + dataName;
}

} // namespace

Domain::Domain(not_null<Main::Domain*> owner, const QString &dataName)
: _owner(owner)
, _dataName(dataName) {
}

Domain::~Domain() = default;

StartResult Domain::start(const QByteArray &passcode) {
	const auto modern = startModern(passcode);
	if (modern == StartModernResult::Success) {
		if (_oldVersion < AppVersion) {
			writeAccounts();
		}
		return StartResult::Success;
	} else if (modern == StartModernResult::IncorrectPasscode) {
		return StartResult::IncorrectPasscode;
	} else if (modern == StartModernResult::Failed) {
		startFromScratch();
		return StartResult::Success;
	}
	auto legacy = std::make_unique<Main::Account>(_owner, _dataName, 0);
	const auto result = legacy->legacyStart(passcode);
	if (result == StartResult::Success) {
		_oldVersion = legacy->local().oldMapVersion();
		startWithSingleAccount(passcode, std::move(legacy));
	}
	return result;
}

void Domain::startAdded(
		not_null<Main::Account*> account,
		std::unique_ptr<MTP::Config> config) {
	Expects(_localKey != nullptr);

	if (restrictedProfile()) {
		for (const auto &entry : _owner->accounts()) {
			if (entry.account.get() == account.get()) {
				for (auto &profile : _accountProfiles) {
					if (profile.id == _activeProfile) {
						profile.accounts.emplace(entry.index);
					}
				}
			}
		}
	}
	account->prepareToStartAdded(_localKey);
	account->start(std::move(config));
}

void Domain::startWithSingleAccount(
		const QByteArray &passcode,
		std::unique_ptr<Main::Account> account) {
	Expects(account != nullptr);
	applyDisguise();

	if (auto localKey = account->local().peekLegacyLocalKey()) {
		_localKey = std::move(localKey);
		encryptLocalKey(passcode);
		account->start(nullptr);
	} else {
		generateLocalKey();
		account->start(account->prepareToStart(_localKey));
	}
	_owner->accountAddedInStorage(Main::Domain::AccountWithIndex{
		.account = std::move(account)
	});
	writeAccounts();
}

void Domain::generateLocalKey() {
	Expects(_localKey == nullptr);
	Expects(_passcodeKeySalt.isEmpty());
	Expects(_passcodeKeyEncrypted.isEmpty());

	auto pass = QByteArray(MTP::AuthKey::kSize, Qt::Uninitialized);
	auto salt = QByteArray(LocalEncryptSaltSize, Qt::Uninitialized);
	base::RandomFill(pass.data(), pass.size());
	base::RandomFill(salt.data(), salt.size());
	_localKey = CreateLocalKey(pass, salt);

	encryptLocalKey(QByteArray());
}

void Domain::encryptLocalKey(const QByteArray &passcode) {
	_passcodeKeySalt.resize(LocalEncryptSaltSize);
	base::RandomFill(_passcodeKeySalt.data(), _passcodeKeySalt.size());
	_passcodeKey = CreateLocalKey(passcode, _passcodeKeySalt);

	EncryptedDescriptor passKeyData(MTP::AuthKey::kSize);
	_localKey->write(passKeyData.stream);
	_passcodeKeyEncrypted = PrepareEncrypted(passKeyData, _passcodeKey);
	_hasLocalPasscode = !passcode.isEmpty();
}

Domain::StartModernResult Domain::startModern(
		const QByteArray &passcode) {
	const auto name = ComputeKeyName(_dataName);

	FileReadDescriptor keyData;
	if (!ReadFile(keyData, name, BaseGlobalPath())) {
		for (const auto suffix : { 's', '0', '1' }) {
			if (QFile::exists(BaseGlobalPath() + name + QChar(suffix))) {
				return StartModernResult::IncorrectPasscode;
			}
		}
		return StartModernResult::Empty;
	}
	LOG(("App Info: reading accounts info..."));

	QByteArray salt, keyEncrypted, infoEncrypted, profileKeys;
	keyData.stream >> salt >> keyEncrypted >> infoEncrypted;
	if (!keyData.stream.atEnd()) {
		keyData.stream >> profileKeys;
	}
	if (!CheckStreamStatus(keyData.stream)) {
		return StartModernResult::IncorrectPasscode;
	}

	if (salt.size() != LocalEncryptSaltSize) {
		LOG(("App Error: bad salt in info file, size: %1").arg(salt.size()));
		return StartModernResult::IncorrectPasscode;
	}
	_passcodeKey = CreateLocalKey(passcode, salt);

	EncryptedDescriptor keyInnerData, info;
	_activeProfile.clear();
	_accountProfiles.clear();
	_storedAccounts.clear();
	_loadedAccounts.clear();
	if (DecryptLocal(keyInnerData, keyEncrypted, _passcodeKey)) {
		auto key = Serialize::read<MTP::AuthKey::Data>(keyInnerData.stream);
		if (keyInnerData.stream.status() != QDataStream::Ok
			|| !keyInnerData.stream.atEnd()) {
			return StartModernResult::IncorrectPasscode;
		}
		_localKey = std::make_shared<MTP::AuthKey>(key);
	} else {
		if (passcode.isEmpty() || profileKeys.size() > 65536) {
			return StartModernResult::IncorrectPasscode;
		}
		auto slots = QDataStream(profileKeys);
		slots.setVersion(QDataStream::Qt_5_1);
		auto count = quint32();
		slots >> count;
		if (count > kMaxProfiles) {
			return StartModernResult::IncorrectPasscode;
		}
		for (auto i = 0U; i != count; ++i) {
			auto slotSalt = QByteArray();
			auto slotKey = QByteArray();
			slots >> slotSalt >> slotKey;
			if (slots.status() != QDataStream::Ok) {
				return StartModernResult::IncorrectPasscode;
			}
			const auto id = decryptProfileKey(
				passcode, slotSalt, slotKey, _localKey);
			if (!id.isEmpty()) {
				_activeProfile = id;
				break;
			}
		}
		if (_activeProfile.isEmpty()) {
			return StartModernResult::IncorrectPasscode;
		}
	}

	_passcodeKeyEncrypted = keyEncrypted;
	_passcodeKeySalt = salt;
	_hasLocalPasscode = !passcode.isEmpty();

	if (!DecryptLocal(info, infoEncrypted, _localKey)) {
		LOG(("App Error: could not decrypt info."));
		return StartModernResult::IncorrectPasscode;
	}
	LOG(("App Info: reading encrypted info..."));
	auto count = qint32();
	info.stream >> count;
	if (!Fork::Accounts::ValidStoredCount(count, info.stream.device()->bytesAvailable())) {
		LOG(("App Error: bad accounts count: %1").arg(count));
		return StartModernResult::IncorrectPasscode;
	}

	_oldVersion = keyData.version;

	auto tried = Fork::AccountProfiles::Indices();
	for (auto i = 0; i != count; ++i) {
		auto index = qint32();
		info.stream >> index;
		if (index < 0 || index >= Fork::Accounts::kNoLimit
			|| !tried.emplace(index).second) {
			return StartModernResult::IncorrectPasscode;
		}
		_storedAccounts.push_back(index);
	}
	auto active = _storedAccounts.front();
	if (!info.stream.atEnd()) {
		info.stream >> active;
	}
	if (!info.stream.atEnd()) {
		auto magic = quint32();
		auto profiles = quint32();
		info.stream >> magic >> profiles;
		if (magic != kProfilesMagic || profiles > kMaxProfiles) {
			return StartModernResult::IncorrectPasscode;
		}
		auto ids = std::set<QByteArray>();
		for (auto i = 0U; i != profiles; ++i) {
			auto profile = AccountProfile();
			auto accounts = quint32();
			info.stream >> profile.id >> profile.name >> accounts;
			if (profile.id.size() != 16 || profile.name.size() > 128
				|| !ids.emplace(profile.id).second
				|| accounts > info.stream.device()->bytesAvailable() / 4) {
				return StartModernResult::IncorrectPasscode;
			}
			for (auto j = 0U; j != accounts; ++j) {
				auto index = qint32();
				info.stream >> index;
				profile.accounts.emplace(index);
			}
			info.stream >> profile.salt >> profile.encryptedKey;
			if (!Fork::AccountProfiles::ValidIndices(profile.accounts)
				|| profile.salt.size() != LocalEncryptSaltSize
				|| profile.encryptedKey.size() > 1024
				|| profile.encryptedKey.isEmpty()) {
				return StartModernResult::IncorrectPasscode;
			}
			_accountProfiles.push_back(std::move(profile));
		}
	}
	if (!readDisguise(info.stream) || info.stream.status() != QDataStream::Ok) {
		return StartModernResult::IncorrectPasscode;
	}
	if (!_activeProfile.isEmpty()) {
		auto verified = false;
		for (const auto &profile : _accountProfiles) {
			if (profile.id == _activeProfile) {
				auto key = MTP::AuthKeyPtr();
				verified = decryptProfileKey(passcode,
					profile.salt, profile.encryptedKey, key) == profile.id
					&& key && key->equals(_localKey);
			}
		}
		if (!verified) {
			return StartModernResult::IncorrectPasscode;
		}
	}
	const auto selected = Fork::AccountProfiles::SelectAccounts(
		_storedAccounts, profileSelection(_activeProfile));
	if (selected.empty()) {
		return StartModernResult::IncorrectPasscode;
	}
	applyDisguise();
	auto sessions = base::flat_set<uint64>();
	for (const auto index : selected) {
		auto account = std::make_unique<Main::Account>(
			_owner, _dataName, index);
		auto config = account->prepareToStart(_localKey);
		const auto sessionId = account->willHaveSessionUniqueId(config.get());
		if (!sessionId || !sessions.contains(sessionId)) {
			_loadedAccounts.emplace(index);
			account->start(std::move(config));
			_owner->accountAddedInStorage({
				.index = index,
				.account = std::move(account),
			});
			sessions.emplace(sessionId);
		}
	}
	if (!_loadedAccounts.contains(active)) {
		active = selected.front();
	}

	_owner->activateFromStorage(active);

	Ensures(!sessions.empty());
	return StartModernResult::Success;
}

void Domain::writeAccounts() {
	Expects(!_owner->accounts().empty());

	const auto path = BaseGlobalPath();
	if (!QDir().exists(path)) {
		QDir().mkpath(path);
	}

	auto current = std::vector<int>();
	for (const auto &[index, account] : _owner->accounts()) {
		current.push_back(index);
	}
	const auto merged = Fork::AccountProfiles::MergeStoredAccounts(
		_storedAccounts, _loadedAccounts, current,
		profileSelection(_activeProfile));
	if (!merged) {
		return;
	}
	_storedAccounts = *merged;
	_loadedAccounts = Fork::AccountProfiles::Indices(
		begin(current), end(current));
	EncryptedDescriptor keyData(0);
	keyData.stream << qint32(_storedAccounts.size());
	for (const auto index : _storedAccounts) {
		keyData.stream << qint32(index);
	}
	keyData.stream << qint32(_owner->activeForStorage());
	keyData.stream << kProfilesMagic << quint32(_accountProfiles.size());
	auto profileKeys = QByteArray();
	auto slots = QDataStream(&profileKeys, QIODevice::WriteOnly);
	slots.setVersion(QDataStream::Qt_5_1);
	slots << quint32(_accountProfiles.size());
	for (const auto &profile : _accountProfiles) {
		keyData.stream << profile.id << profile.name
			<< quint32(profile.accounts.size());
		for (const auto index : profile.accounts) {
			keyData.stream << qint32(index);
		}
		keyData.stream << profile.salt << profile.encryptedKey;
		slots << profile.salt << profile.encryptedKey;
	}
	writeDisguise(keyData.stream);
	FileWriteDescriptor key(ComputeKeyName(_dataName), path, true);
	key.writeData(_passcodeKeySalt);
	key.writeData(_passcodeKeyEncrypted);
	key.writeEncrypted(keyData, _localKey);
	key.writeData(profileKeys);
}

void Domain::startFromScratch() {
	startWithSingleAccount(
		QByteArray(),
		std::make_unique<Main::Account>(_owner, _dataName, 0));
}

bool Domain::checkPasscode(const QByteArray &passcode) const {
	Expects(!_passcodeKeySalt.isEmpty());
	const auto checkKey = CreateLocalKey(passcode, _passcodeKeySalt);
	auto data = EncryptedDescriptor();
	if (!DecryptLocal(data, _passcodeKeyEncrypted, checkKey)) {
		return false;
	}
	const auto raw = Serialize::read<MTP::AuthKey::Data>(data.stream);
	return data.stream.status() == QDataStream::Ok
		&& data.stream.atEnd()
		&& MTP::AuthKey(raw).equals(_localKey);
}

bool Domain::setPasscode(const QByteArray &passcode) {
	Expects(!_passcodeKeySalt.isEmpty());
	Expects(_localKey != nullptr);

	if (restrictedProfile()) {
		return false;
	}
	if (!passcode.isEmpty()) {
		for (const auto &profile : _accountProfiles) {
			auto key = MTP::AuthKeyPtr();
			if (!decryptProfileKey(passcode, profile.salt,
					profile.encryptedKey, key).isEmpty()) {
				return false;
			}
		}
	}
	if (passcode.isEmpty()) {
		_accountProfiles.clear();
		_accountProfilesChanged.fire({});
	}
	encryptLocalKey(passcode);
	writeAccounts();

	_passcodeKeyChanged.fire({});
	return true;
}

int Domain::oldVersion() const {
	return _oldVersion;
}

void Domain::clearOldVersion() {
	_oldVersion = 0;
}

rpl::producer<> Domain::localPasscodeChanged() const {
	return _passcodeKeyChanged.events();
}

bool Domain::hasLocalPasscode() const {
	return _hasLocalPasscode;
}

} // namespace Storage

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "fork/account_profile_policy.h"

namespace MTP {
class Config;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace Main {
class Account;
class Domain;
} // namespace Main

namespace Storage {

enum class StartResult : uchar {
	Success,
	IncorrectPasscode,
	IncorrectPasscodeLegacy,
};

class Domain final {
public:
	struct AccountProfile {
		QByteArray id;
		QString name;
		Fork::AccountProfiles::Indices accounts;
		QByteArray salt;
		QByteArray encryptedKey;
	};
	Domain(not_null<Main::Domain*> owner, const QString &dataName);
	~Domain();

	[[nodiscard]] StartResult start(const QByteArray &passcode);
	void startAdded(
		not_null<Main::Account*> account,
		std::unique_ptr<MTP::Config> config);
	void writeAccounts();
	void startFromScratch();

	[[nodiscard]] bool checkPasscode(const QByteArray &passcode) const;
	bool setPasscode(const QByteArray &passcode);
	[[nodiscard]] bool tryUnlockPasscode(const QByteArray &passcode);
	[[nodiscard]] bool applyPendingProfile();
	[[nodiscard]] bool restrictedProfile() const;
	[[nodiscard]] bool hasAccountProfiles() const;
	[[nodiscard]] const std::vector<AccountProfile> &accountProfiles() const;
	[[nodiscard]] bool saveAccountProfile(
		const QByteArray &id,
		const QString &name,
		const Fork::AccountProfiles::Indices &accounts,
		const QByteArray &passcode);
	[[nodiscard]] bool removeAccountProfile(const QByteArray &id);
	[[nodiscard]] bool accountIndexReserved(int index) const;
	[[nodiscard]] rpl::producer<> accountProfilesChanged() const;

	[[nodiscard]] int oldVersion() const;
	void clearOldVersion();

	[[nodiscard]] rpl::producer<> localPasscodeChanged() const;
	[[nodiscard]] bool hasLocalPasscode() const;

private:
	enum class StartModernResult {
		Success,
		IncorrectPasscode,
		Failed,
		Empty,
	};

	[[nodiscard]] StartModernResult startModern(const QByteArray &passcode);
	void startWithSingleAccount(
		const QByteArray &passcode,
		std::unique_ptr<Main::Account> account);
	void generateLocalKey();
	void encryptLocalKey(const QByteArray &passcode);
	[[nodiscard]] Fork::AccountProfiles::Selection profileSelection(
		const QByteArray &id) const;
	[[nodiscard]] QByteArray decryptProfileKey(
		const QByteArray &passcode,
		const QByteArray &salt,
		const QByteArray &encrypted,
		MTP::AuthKeyPtr &key) const;

	const not_null<Main::Domain*> _owner;
	const QString _dataName;

	MTP::AuthKeyPtr _localKey;
	MTP::AuthKeyPtr _passcodeKey;
	QByteArray _passcodeKeySalt;
	QByteArray _passcodeKeyEncrypted;
	int _oldVersion = 0;

	bool _hasLocalPasscode = false;
	rpl::event_stream<> _passcodeKeyChanged;
	std::vector<AccountProfile> _accountProfiles;
	std::vector<int> _storedAccounts;
	Fork::AccountProfiles::Indices _loadedAccounts;
	QByteArray _activeProfile;
	std::optional<QByteArray> _pendingProfile;
	rpl::event_stream<> _accountProfilesChanged;

};

} // namespace Storage

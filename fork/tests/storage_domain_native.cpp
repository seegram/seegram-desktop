// Opt-in regression scenario for the actual storage implementation. Run only
// in a disposable -workdir; no authorization files or real accounts are used.
#include "storage/storage_domain.h"
#include "storage/details/storage_file_utilities.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "mtproto/mtproto_auth_key.h"
#include "core/application.h"
#include "base/openssl_help.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <cstdlib>
#include <iostream>

namespace Fork::Tests {
namespace {

using namespace Storage;
using namespace Storage::details;
auto Checks = 0;

void Require(bool condition, const char *message) {
	if (!condition) {
		std::cerr << "FAILED: " << message << std::endl;
		std::_Exit(2);
	}
	++Checks;
}

QByteArray ReadBytes(const QString &path) {
	auto file = QFile(path);
	Require(file.open(QIODevice::ReadOnly), "read fixture");
	return file.readAll();
}

QString KeyPath(const QString &name) {
	return cWorkingDir() + u"tdata/key_"_q + name + 's';
}

QByteArray GroupId() {
	return QByteArray(16, 'g');
}

void LegacyFixture(const QString &name, bool mismatchedId = false) {
	const auto key = CreateLocalKey(QByteArray("fixture-key"), QByteArray(32, 'k'));
	const auto salt = QByteArray(32, 'm');
	auto master = EncryptedDescriptor(0);
	key->write(master.stream);
	const auto wrapped = PrepareEncrypted(master, CreateLocalKey("master", salt));

	const auto groupSalt = QByteArray(32, 'p');
	auto group = EncryptedDescriptor(0);
	key->write(group.stream);
	group.stream << GroupId();
	const auto groupWrapped = PrepareEncrypted(
		group, CreateLocalKey("group", groupSalt));

	auto info = EncryptedDescriptor(0);
	info.stream << qint32(3) << qint32(0) << qint32(1) << qint32(2) << qint32(0);
	info.stream << quint32(0x53475031) << quint32(1);
	info.stream << (mismatchedId ? QByteArray(16, 'x') : GroupId())
		<< u"Private group"_q << quint32(1) << qint32(1)
		<< groupSalt << groupWrapped;
	// SeeGram 7.2's encrypted appearance tail, including a clean group.
	info.stream << quint32(0x53474D31) << u"Custom name"_q
		<< quint32(0) << quint32(1)
		<< (mismatchedId ? QByteArray(16, 'x') : GroupId()) << quint32(1);

	auto file = FileWriteDescriptor(u"key_"_q + name, cWorkingDir() + u"tdata/"_q, true);
	file.writeData(salt);
	file.writeData(wrapped);
	file.writeEncrypted(info, key);
	file.writeData(Fork::AccountProfiles::WriteKeys({{
		.salt = groupSalt,
		.encryptedKey = groupWrapped,
	}}));
	Require(file.finish(), "write legacy fixture");
}

StartResult Open(Main::Domain &domain, const QByteArray &passcode) {
	auto &local = domain.local();
	const auto probe = local.start(local.prepareOpen({}));
	if (passcode.isEmpty() || probe == StartResult::Success) {
		return probe;
	}
	auto derived = local.prepareOpen(passcode);
	derived.run();
	return local.start(std::move(derived));
}

bool Check(Storage::Domain &local, const QByteArray &passcode) {
	auto derived = local.prepareOpen(passcode);
	derived.run();
	return local.checkPasscode(std::move(derived));
}

bool Unlock(Storage::Domain &local, const QByteArray &passcode) {
	auto derived = local.prepareOpen(passcode);
	derived.run();
	return local.tryUnlockPasscode(std::move(derived));
}

void CheckLayout(const QString &name, bool modern, bool groups) {
	auto file = FileReadDescriptor();
	Require(ReadFile(file, u"key_"_q + name, cWorkingDir() + u"tdata/"_q), "read key layout");
	auto salt = QByteArray(), key = QByteArray(), info = QByteArray(), trailer = QByteArray();
	file.stream >> salt >> key >> info >> trailer;
	auto inner = QDataStream(trailer);
	inner.setVersion(QDataStream::Qt_5_1);
	auto magic = quint32();
	inner >> magic;
	Require((magic == 0x4B443200) == modern, "correct key-data format");
	if (modern) {
		Require(file.stream.atEnd() != groups, "profile slots follow upstream trailer");
		if (groups) {
			file.stream >> trailer;
		}
	}
	if (groups) {
		auto parsed = std::vector<Fork::AccountProfiles::PasscodeKey>();
		Require(Fork::AccountProfiles::ReadKeys(trailer, parsed)
			&& parsed.size() == 1, "group key slots survive serialization");
	}
}

} // namespace

void RunStorageRegression() {
	const auto directory = QFileInfo(cWorkingDir()).canonicalFilePath();
	Require(directory.startsWith(u"/private/tmp/seegram-730-storage-"_q)
		&& QFile::exists(directory + u"/.native-storage-test"_q),
		"native storage tests require an explicitly marked disposable directory");
	QDir().mkpath(cWorkingDir() + u"tdata/"_q);

	const auto name = u"native-legacy"_q;
	LegacyFixture(name);
	const auto original = ReadBytes(KeyPath(name));
	{
		auto domain = Main::Domain(name);
		Require(Open(domain, "wrong") == StartResult::IncorrectPasscode, "wrong legacy passcode rejected");
		Require(domain.local().hasAccountProfiles(), "groups disable biometric unlock before decryption");
		Require(ReadBytes(KeyPath(name)) == original, "wrong passcode does not rewrite the file");
	}
	{
		auto domain = Main::Domain(name);
		Require(Open(domain, "group") == StartResult::Success, "legacy group opens before master migration");
		auto &local = domain.local();
		Require(domain.accounts().size() == 1 && domain.accounts().front().index == 1,
			"legacy group loads only its account");
		Require(local.restrictedProfile() && local.cleanProfile(), "clean-group state restored");
		Require(local.accountProfiles().empty(), "group metadata hidden in restricted view");
		Require(!local.verifyPasscode("group"), "group passcode cannot mint a master token");
		Require(!local.removeAccountProfile(GroupId()), "restricted deletion rejected");
		local.clearPasscodeAfterReset();
		Require(local.hasPasscode(), "restricted reset cannot remove master passcode");
		local.writeAccounts();
		Sync();
		CheckLayout(name, false, true);
		Require(Unlock(local, "master"), "master unlock after entering an old group");
		Sync();
		CheckLayout(name, true, true);
	}
	{
		auto domain = Main::Domain(name);
		Require(Open(domain, "master") == StartResult::Success, "master opens migrated storage");
		auto &local = domain.local();
		Require(domain.accounts().size() == 3, "hidden accounts survive restricted writes");
		Require(!local.restrictedProfile() && local.accountProfiles().size() == 1,
			"master retains group management");
		Require(local.disguiseSettings().name == u"Custom name"_q, "appearance survives migration");
		Require(!Check(local, "group") && Check(local, "master"), "only master verifies administrative actions");
		Require(local.setAppLockEnabled(false, *local.verifyPasscode("master"))
			== SetPasscodeResult::Failed, "groups keep application locking enabled");
		auto duplicate = local.prepareNewWrap("group");
		duplicate.run();
		Require(local.setPasscode(std::move(duplicate), *local.verifyPasscode("master"))
			== SetPasscodeResult::Failed, "master cannot collide with a group passcode");
		Require(!local.saveAccountProfile(GroupId(), u"Private group"_q, { 1 }, "master"),
			"group cannot collide with master passcode");
		auto proof = local.verifyPasscode("master");
		Require(proof.has_value(), "master verification available");
		auto replacement = local.prepareNewWrap("new-master");
		replacement.run();
		Require(local.setPasscode(std::move(replacement), *proof) == SetPasscodeResult::Success,
			"master passcode changes through upstream staged writes");
		Require(local.setPasscode("reused-proof", *proof) == SetPasscodeResult::NeedsVerification,
			"verification proof cannot be reused");
		Require(!Check(local, "master") && Check(local, "new-master"), "old master no longer works");
		Require(Unlock(local, "group"), "group survives master passcode change");
		Sync();
	}
	{
		auto domain = Main::Domain(name);
		Require(Open(domain, "group") == StartResult::Success, "cold group opens modern key-data format");
		Require(domain.accounts().size() == 1 && domain.accounts().front().index == 1,
			"modern group still loads only its account");
		Require(domain.local().cleanProfile(), "modern clean group remains clean");
		Require(domain.local().setPasscode("forbidden", *domain.local().verifyPasscode("new-master"))
			== SetPasscodeResult::Failed, "restricted mutation denied even with a valid proof");
		Sync();
	}
	{
		auto domain = Main::Domain(name);
		Require(Open(domain, "new-master") == StartResult::Success, "new master opens every account");
		auto &local = domain.local();
		auto stale = local.prepareOpen("group");
		stale.run();
		auto staleMaster = local.prepareNewWrap("new-group");
		staleMaster.run();
		Require(local.saveAccountProfile(GroupId(), u"Private group"_q, { 1 }, "new-group"),
			"group passcode changes");
		Require(!local.tryUnlockPasscode(std::move(stale)), "stale group derivation rejected after replacement");
		Require(local.setPasscode(std::move(staleMaster), *local.verifyPasscode("new-master"))
			== SetPasscodeResult::Failed, "group changes invalidate an in-flight master derivation");
		Require(Unlock(local, "new-group"), "replacement group passcode works");
		Require(local.setPasscode(QByteArray(), *local.verifyPasscode("new-master"))
			== SetPasscodeResult::Success, "master can remove passcode and groups");
		Require(!local.hasPasscode() && !local.hasAccountProfiles(), "groups removed with master passcode");
		Sync();
		CheckLayout(name, true, false);
	}
	{
		auto domain = Main::Domain(name);
		Require(Open(domain, {}) == StartResult::Success && domain.accounts().size() == 3,
			"all accounts reopen without passcode after confirmed removal");
		Require(domain.local().disguiseSettings().name == u"Custom name"_q,
			"removing clean groups preserves valid appearance metadata");
		Sync();
	}

	const auto direct = u"native-direct-migration"_q;
	LegacyFixture(direct);
	{
		auto domain = Main::Domain(direct);
		Require(Open(domain, "master") == StartResult::Success, "direct legacy master migration");
		Require(domain.accounts().size() == 3 && domain.local().hasAccountProfiles(),
			"direct migration retains accounts and group definitions");
		Sync();
		CheckLayout(direct, true, true);
	}
	{
		auto domain = Main::Domain(direct);
		Require(Open(domain, "group") == StartResult::Success, "group opens after direct migration");
		Sync();
	}
	const auto mismatch = u"native-mismatched-slot"_q;
	LegacyFixture(mismatch, true);
	const auto mismatchedBytes = ReadBytes(KeyPath(mismatch));
	{
		auto domain = Main::Domain(mismatch);
		Require(Open(domain, "group") == StartResult::IncorrectPasscode,
			"unauthenticated slot identity must match encrypted profile metadata");
		Require(ReadBytes(KeyPath(mismatch)) == mismatchedBytes, "refused profile never overwrites storage");
	}
	const auto corrupt = u"native-corrupt"_q;
	{
		auto file = QFile(KeyPath(corrupt));
		Require(file.open(QIODevice::WriteOnly), "create corrupt fixture");
		file.write("truncated");
	}
	{
		auto domain = Main::Domain(corrupt);
		Require(Open(domain, "master") == StartResult::IncorrectPasscode, "corrupt key file fails closed");
		Require(ReadBytes(KeyPath(corrupt)) == "truncated", "corrupt key file is never replaced with empty storage");
	}
	std::cout << "PASS: " << Checks << " native storage checks" << std::endl;
	std::_Exit(0);
}

} // namespace Fork::Tests

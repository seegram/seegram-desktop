#pragma once

#include <QtCore/QByteArray>
#include <memory>
#include <vector>

namespace MTP {
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace Fork::AccountProfiles {

struct PasscodeKey {
	QByteArray salt;
	QByteArray encryptedKey;
	QByteArray id;
	MTP::AuthKeyPtr localKey;
};

[[nodiscard]] bool ReadKeys(
	const QByteArray &bytes,
	std::vector<PasscodeKey> &keys);
[[nodiscard]] QByteArray WriteKeys(const std::vector<PasscodeKey> &keys);
[[nodiscard]] QByteArray DecryptKey(
	const QByteArray &passcode,
	const QByteArray &salt,
	const QByteArray &encrypted,
	MTP::AuthKeyPtr &key);

} // namespace Fork::AccountProfiles

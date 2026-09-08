#include "fork/account_profile_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

} // namespace

int main() {
	using namespace Fork::AccountProfiles;
	const auto master = Selection();
	const auto restricted = Selection{ Indices{ 1, 3 } };
	const auto empty = Selection{ Indices() };
	const auto stored = std::vector{ 0, 1, 2, 3 };
	Require(master.master() && !restricted.master() && !empty.master(),
		"An empty profile must never be treated as the master profile.");
	Require(SelectAccounts(stored, empty).empty(),
		"An empty profile must never fall back to all accounts.");
	Require(SelectAccounts(stored, restricted) == std::vector({ 1, 3 }),
		"Only explicitly assigned accounts may start.");
	Require(SelectAccounts({ -1, 3, 3, 1, Fork::Accounts::kNoLimit }, master)
		== std::vector({ 3, 1 }), "Invalid and duplicate indices must be rejected.");
	Require(MergeStoredAccounts(stored, { 1, 3 }, { 1, 3 }, restricted)
		== stored, "Saving a restricted profile must retain hidden accounts.");
	Require(MergeStoredAccounts(stored, { 1, 3 }, { 3 }, restricted)
		== std::vector({ 0, 2, 3 }),
		"Logging out of a visible account must retain hidden accounts.");
	Require(MergeStoredAccounts(stored, { 1, 3 }, {}, restricted)
		== std::vector({ 0, 2 }),
		"Logging out of all visible accounts must not erase hidden accounts.");
	Require(MergeStoredAccounts(stored, {}, {}, empty) == stored,
		"Unloaded sessions must survive even if none can be started.");
	Require(!MergeStoredAccounts(stored, { 0, 1 }, { 1 }, restricted),
		"A restricted write must fail if an unassigned account was loaded.");
	Require(!MergeStoredAccounts(stored, { 1 }, { 0, 1 }, restricted),
		"A restricted write must not admit an unassigned account.");
	Require(!MergeStoredAccounts(stored, { 1 }, { 1, 1 }, restricted),
		"Duplicate current account indices must fail the write.");
	Require(MergeStoredAccounts(stored, { 0, 1, 2, 3 }, { 3, 4 }, master)
		== std::vector({ 3, 4 }), "Master can remove and add accounts.");
	Require(NextAccountIndex({ 0, 2 }, { 1, 4 }) == 3,
		"New accounts must not reuse a hidden or previously assigned index.");
	Require(!NextAccountIndex({ -1 }, {}),
		"Corrupt stored indices must fail closed.");
	Require(!NextAccountIndex({}, { Fork::Accounts::kNoLimit }),
		"Invalid assignments must fail closed.");
}

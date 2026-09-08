#include "fork/account_limits.h"
using Fork::Accounts::ValidStoredCount;
static_assert(ValidStoredCount(1, 4));
static_assert(ValidStoredCount(7, 28));
static_assert(ValidStoredCount(100, 400));
static_assert(ValidStoredCount(100, 404));
static_assert(!ValidStoredCount(100, 399));
static_assert(!ValidStoredCount(0, 4));
static_assert(!ValidStoredCount(-1, 400));
static_assert(!ValidStoredCount(7, -1));
static_assert(!ValidStoredCount(2147483647, 4));
static_assert(Fork::Accounts::kNoLimit + 1 > Fork::Accounts::kNoLimit);
int main() {}

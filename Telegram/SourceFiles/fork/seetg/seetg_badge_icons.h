#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <rpl/producer.h>
namespace Fork::SeeTg::BadgeIcons {
void Request(const QString &type);
[[nodiscard]] QByteArray Cached(const QString &type);
[[nodiscard]] rpl::producer<> Changes();
} // namespace Fork::SeeTg::BadgeIcons

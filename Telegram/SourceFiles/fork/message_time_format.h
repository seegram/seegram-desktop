#pragma once

#include <QtCore/QLocale>
#include <QtCore/QTime>

namespace Fork::Marks {

[[nodiscard]] inline QString FormatMessageTime(
		QTime time,
		const QLocale &locale,
		bool seconds) {
	auto format = locale.timeFormat(QLocale::ShortFormat);
	if (seconds) {
		auto quoted = false;
		for (auto i = 0; i < format.size(); ++i) {
			if (format[i] == QLatin1Char('\'')) {
				quoted = !quoted;
			} else if (!quoted && format[i] == QLatin1Char('m')) {
				while (i + 1 < format.size() && format[i + 1] == QLatin1Char('m')) ++i;
				format.insert(i + 1, QStringLiteral(":ss"));
				break;
			}
		}
	}
	return locale.toString(time, format);
}

} // namespace Fork::Marks

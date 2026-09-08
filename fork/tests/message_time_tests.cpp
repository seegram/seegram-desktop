#include "fork/message_time_format.h"
#include <iostream>

int main() {
	using Fork::Marks::FormatMessageTime;
	const auto time = QTime(13, 4, 5);
	const auto ru = QLocale(QLocale::Russian, QLocale::Russia);
	const auto en = QLocale(QLocale::English, QLocale::UnitedStates);
	if (FormatMessageTime(time, ru, true) != QStringLiteral("13:04:05")) return 1;
	if (FormatMessageTime(time, en, true).replace(QChar(0x202F), QLatin1Char(' ')) != QStringLiteral("1:04:05 PM")) return 2;
	if (FormatMessageTime(QTime(0, 4, 5), en, true).replace(QChar(0x202F), QLatin1Char(' ')) != QStringLiteral("12:04:05 AM")) return 3;
	for (const auto locale : { ru, en, QLocale(QLocale::Arabic), QLocale(QLocale::Japanese) }) {
		if (FormatMessageTime(time, locale, false) != locale.toString(time, QLocale::ShortFormat)) return 4;
	}
	std::cout << "Message time: seconds, 12/24-hour format, midnight and unchanged defaults passed\n";
}

/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_http.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Fork::SeeTg::Http {
namespace {

[[nodiscard]] QNetworkAccessManager &Manager() {
	static auto manager = QNetworkAccessManager();
	return manager;
}

void Finish(not_null<QNetworkReply*> reply, const Callback &done) {
	reply->deleteLater();
	auto result = Response();
	result.status = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
	result.body = reply->readAll();
	result.etag = reply->rawHeader("ETag");
	if (reply->error() != QNetworkReply::NoError && !result.status) {
		result.error = reply->errorString();
	}
	done(result);
}

} // namespace

void Post(
		const QString &url,
		const std::vector<Header> &headers,
		const QByteArray &body,
		crl::time timeout,
		Callback done) {
	auto request = QNetworkRequest(QUrl(url));
	for (const auto &header : headers) {
		request.setRawHeader(header.name, header.value);
	}
	request.setTransferTimeout(timeout);
	const auto reply = Manager().post(request, body);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		Finish(reply, done);
	});
}

void Get(const QString &url, crl::time timeout, Callback done) {
	Get(url, {}, timeout, std::move(done));
}

void Get(const QString &url, const std::vector<Header> &headers, crl::time timeout, Callback done) {
	auto request = QNetworkRequest(QUrl(url));
	request.setTransferTimeout(timeout);
	for (const auto &header : headers) {
		request.setRawHeader(header.name, header.value);
	}
	const auto reply = Manager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		Finish(reply, done);
	});
}

} // namespace Fork::SeeTg::Http

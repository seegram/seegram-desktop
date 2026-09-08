/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Plain HTTPS for the see.tg integration.
//
// On macOS this deliberately bypasses Qt: the project's Qt is built on
// SecureTransport, and the first TLS handshake makes Qt walk the keychain
// for root certificates inside the process, which is where a current macOS
// beta trips a libmalloc assertion and takes the client down. NSURLSession
// evaluates trust in a system daemon instead, so the client never touches
// the keychain. Elsewhere Qt links OpenSSL and is fine.
//
// Callbacks run on the main thread and are not guarded; the caller wraps
// them in crl::guard when it can die first.

namespace Fork::SeeTg::Http {

struct Header {
	QByteArray name;
	QByteArray value;
};

struct Response {
	int status = 0; // 0 when the request never reached a server.
	QByteArray body;
	QByteArray etag;
	QString error;
};

using Callback = Fn<void(Response)>;

void CancelAll();

void Post(
	const QString &url,
	const std::vector<Header> &headers,
	const QByteArray &body,
	crl::time timeout,
	Callback done);

void Get(
	const QString &url,
	crl::time timeout,
	Callback done);

void Get(const QString &url, const std::vector<Header> &headers, crl::time timeout, Callback done);

} // namespace Fork::SeeTg::Http

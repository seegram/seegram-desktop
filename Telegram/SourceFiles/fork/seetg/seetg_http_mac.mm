/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_http.h"
#include "fork/disguise.h"

#include <crl/crl_on_main.h>

#import <Foundation/Foundation.h>

namespace Fork::SeeTg::Http {
namespace {

NSURLSession *Session = nil;

void Send(NSMutableURLRequest *request, Callback done) {
	if (Disguise::Clean()) {
		done({ .error = u"integration disabled"_q });
		return;
	}
	const auto generation = Disguise::ScopeGeneration();
	const auto callback = std::make_shared<Callback>(std::move(done));
	const auto handler = ^(NSData *data, NSURLResponse *response, NSError *error) {
		auto result = Response();
		if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
			result.status = int(((NSHTTPURLResponse*)response).statusCode);
			for (NSString *key in ((NSHTTPURLResponse*)response).allHeaderFields) {
				if ([key caseInsensitiveCompare:@"ETag"] == NSOrderedSame) {
					result.etag = QString::fromNSString([((NSHTTPURLResponse*)response).allHeaderFields[key] description]).toUtf8();
				}
			}
		}
		if (data) {
			result.body = QByteArray(
				static_cast<const char*>(data.bytes),
				int(data.length));
		}
		if (error) {
			result.error = QString::fromNSString(error.localizedDescription);
		}
		crl::on_main([=] {
			(*callback)((generation == Disguise::ScopeGeneration()) ? result
				: Response{ .error = u"integration disabled"_q });
		});
	};
	if (!Session) {
		Session = [[NSURLSession sessionWithConfiguration:
			[NSURLSessionConfiguration ephemeralSessionConfiguration]] retain];
	}
	[[Session
		dataTaskWithRequest:request
		completionHandler:handler] resume];
}

[[nodiscard]] NSMutableURLRequest *Make(
		const QString &url,
		crl::time timeout) {
	NSMutableURLRequest *request = [NSMutableURLRequest
		requestWithURL:[NSURL URLWithString:url.toNSString()]];
	request.timeoutInterval = timeout / 1000.;
	request.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
	return request;
}

} // namespace

void CancelAll() {
	[Session invalidateAndCancel];
	[Session release];
	Session = nil;
}

void Post(
		const QString &url,
		const std::vector<Header> &headers,
		const QByteArray &body,
		crl::time timeout,
		Callback done) {
	@autoreleasepool {
		NSMutableURLRequest *request = Make(url, timeout);
		request.HTTPMethod = @"POST";
		for (const auto &header : headers) {
			[request
				setValue:[NSString stringWithUTF8String:header.value.constData()]
				forHTTPHeaderField:[NSString stringWithUTF8String:header.name.constData()]];
		}
		request.HTTPBody = [NSData dataWithBytes:body.constData() length:body.size()];
		Send(request, std::move(done));
	}
}

void Get(const QString &url, crl::time timeout, Callback done) {
	Get(url, {}, timeout, std::move(done));
}

void Get(const QString &url, const std::vector<Header> &headers, crl::time timeout, Callback done) {
	@autoreleasepool {
		NSMutableURLRequest *request = Make(url, timeout);
		request.HTTPMethod = @"GET";
		for (const auto &header : headers) {
			[request setValue:[NSString stringWithUTF8String:header.value.constData()]
				forHTTPHeaderField:[NSString stringWithUTF8String:header.name.constData()]];
		}
		Send(request, std::move(done));
	}
}

} // namespace Fork::SeeTg::Http

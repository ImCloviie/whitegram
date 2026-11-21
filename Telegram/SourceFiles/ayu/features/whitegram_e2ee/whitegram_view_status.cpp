/*
This file is part of Whitegram for Desktop, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ayu/features/whitegram_e2ee/whitegram_view_status.h"

#include "ayu/features/whitegram_backend/whitegram_backend.h"
#include "ayu/features/whitegram_e2ee/whitegram_e2ee.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Whitegram::E2EE {
namespace {

constexpr auto kPollInterval = crl::time(4000);
constexpr auto kWatchFor = crl::time(15000);
constexpr auto kTimeout = 15000;
constexpr auto kMaxHashes = 50;

struct State {
	QNetworkAccessManager network;
	base::flat_map<QString, ViewInfo> views;
	base::flat_map<QString, crl::time> watched;
	base::flat_set<FullMsgId> revoked;
	base::flat_set<not_null<Main::Session*>> tracked;
	Main::Session *session = nullptr;
	std::unique_ptr<base::Timer> timer;
	bool polling = false;
	int64 offset = 0;
	rpl::event_stream<QString> updates;
};

[[nodiscard]] State &Get() {
	static State state;
	return state;
}

[[nodiscard]] int64 LocalNowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

void Track(not_null<Main::Session*> session) {
	auto &state = Get();
	state.session = session;
	if (state.tracked.emplace(session).second) {
		session->lifetime().add([=] {
			auto &state = Get();
			state.tracked.remove(session);
			if (state.session == session) {
				state.session = nullptr;
			}
		});
	}
}

[[nodiscard]] QNetworkRequest Request(
		not_null<Main::Session*> session,
		const QString &path) {
	Backend::EnsureOwnerToken(session);
	auto request = QNetworkRequest(QUrl(Backend::BaseUrl() + path));
	request.setTransferTimeout(kTimeout);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	Backend::ApplyTlsPinning(request);
	Backend::ApplyOwnerToken(request, session->userId().bare);
	return request;
}

void Poll() {
	auto &state = Get();
	if (state.polling || !state.session) {
		return;
	}
	const auto now = crl::now();
	auto hashes = QStringList();
	for (auto i = state.watched.begin(); i != state.watched.end();) {
		if (now - i->second > kWatchFor || state.views.contains(i->first)) {
			i = state.watched.erase(i);
		} else {
			if (hashes.size() < kMaxHashes) {
				hashes.push_back(i->first);
			}
			++i;
		}
	}
	if (hashes.isEmpty()) {
		state.timer->cancel();
		return;
	}
	state.polling = true;
	const auto reply = state.network.get(Request(
		state.session,
		u"/api/v1/e2ee/keys/views?h="_q + hashes.join(u',')));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto &state = Get();
		state.polling = false;
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		const auto json = QJsonDocument::fromJson(reply->readAll()).object();
		if (const auto server = int64(json.value(u"now"_q).toDouble())) {
			state.offset = server - LocalNowMs();
		}
		const auto views = json.value(u"views"_q).toObject();
		for (auto i = views.begin(); i != views.end(); ++i) {
			const auto view = i.value().toObject();
			state.views[i.key()] = ViewInfo{
				.openedAt = int64(view.value(u"openedAt"_q).toDouble()),
				.ttl = view.value(u"ttl"_q).toInt(),
			};
			state.watched.remove(i.key());
			state.updates.fire_copy(i.key());
		}
	});
}

} // namespace

int64 ViewInfo::expiresAt() const {
	return (ttl > 0) ? (openedAt + ttl * int64(1000)) : 0;
}

int64 ServerNowMs() {
	return LocalNowMs() + Get().offset;
}

void ReportOpened(
		not_null<Main::Session*> session,
		const QString &payload,
		int ttl) {
	const auto hash = PacketHash(payload);
	if (hash.isEmpty()) {
		return;
	}
	Track(session);
	const auto body = QJsonDocument(QJsonObject{
		{ u"hash"_q, hash },
		{ u"ttl"_q, ttl },
	}).toJson(QJsonDocument::Compact);
	const auto reply = Get().network.post(
		Request(session, u"/api/v1/e2ee/keys/views"_q),
		body);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
	});
}

void WatchViewStatus(not_null<Main::Session*> session, const QString &hash) {
	auto &state = Get();
	if (hash.isEmpty() || state.views.contains(hash)) {
		return;
	}
	Track(session);
	const auto fresh = !state.watched.contains(hash);
	state.watched[hash] = crl::now();
	if (!state.timer) {
		state.timer = std::make_unique<base::Timer>([] { Poll(); });
	}
	if (!state.timer->isActive()) {
		state.timer->callEach(kPollInterval);
	}
	if (fresh) {
		crl::on_main([] { Poll(); });
	}
}

std::optional<ViewInfo> ViewStatus(const QString &hash) {
	const auto &views = Get().views;
	const auto i = views.find(hash);
	return (i != end(views)) ? std::make_optional(i->second) : std::nullopt;
}

rpl::producer<QString> ViewStatusUpdates() {
	return Get().updates.events();
}

void RevokeOnce(not_null<HistoryItem*> item) {
	if (!Get().revoked.emplace(item->fullId()).second) {
		return;
	}
	if (item->isRegular()) {
		item->history()->session().api().request(MTPmessages_DeleteMessages(
			MTP_flags(MTPmessages_DeleteMessages::Flag::f_revoke),
			MTP_vector<MTPint>(1, MTP_int(item->id.bare))
		)).send();
	}
	item->destroy();
}

} // namespace Whitegram::E2EE

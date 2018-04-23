#include "updatechecker.h"
#include "../constants.h"

#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

UpdateChecker::UpdateChecker()
{
	
}

void UpdateChecker::checkForUpdate()
{
	auto reply = manager.get(QNetworkRequest(QUrl(Constants::UPDATE_CHECK_URL + Constants::CONTENT_VERSION)));
	if (reply) {
		connect(reply, &QNetworkReply::finished, [this, reply]() {
			QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
			auto obj = doc.object();

			auto shouldUpdate = obj.value("should_update").toBool(false);
			if (!shouldUpdate) return;

			auto nextVersion = obj.value("id").toString();
			auto versionNotes = obj.value("notes").toString();
			auto downloadLink = obj.value("download_url").toString();

			emit updateNeeded(nextVersion, versionNotes, downloadLink);
		});
	}
}
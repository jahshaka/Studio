#include "updatechecker.h"
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
	//QNetworkAccessManager *manager;
	//QNetworkRequest request;

	//manager = new QNetworkAccessManager();

	auto reply = manager.get(QNetworkRequest(QUrl("http://localhost/jahapi/shouldupdate.php?currentVersion=0.5a")));
	if (reply) {
		connect(reply, &QNetworkReply::finished, [this, reply]() {
			QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
			auto obj = doc.object();

			auto shouldUpdate = obj.value("shouldUpdate").toBool(false);
			if (!shouldUpdate)
				return;

			auto nextVersion = obj.value("nextVersion").toString();
			auto versionNotes = obj.value("versionNotes").toString();
			auto downloadLink = obj.value("downloadLink").toString();
			qDebug() << downloadLink;

			qDebug() << obj;

			emit updateNeeded(nextVersion, versionNotes, downloadLink);

		});
	}
}
/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
void UpdateChecker::checkForPlayerUpdate(){
    url = Constants::PLAYER_CHECK_URL + Constants::CONTENT_VERSION;
    checkForUpdate();
}
void UpdateChecker::checkForAppUpdate(){
    url = Constants::UPDATE_CHECK_URL + Constants::CONTENT_VERSION;
	checkForUpdate();
}
void UpdateChecker::checkForMinerUpdate(){
    url = Constants::MINER_CHECK_URL + Constants::CONTENT_VERSION;
	checkForUpdate();
}

void UpdateChecker::checkForUpdate()
{
	auto reply = manager.get(QNetworkRequest(url));
	if (reply) {
		connect(reply, &QNetworkReply::finished, [this, reply]() {
			QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
			auto obj = doc.object();

			auto shouldUpdate = obj.value("should_update").toBool(false);
			if (!shouldUpdate) return;

			auto nextVersion = obj.value("id").toString();
			auto versionNotes = obj.value("notes").toString();

#ifdef Q_OS_WIN 
			auto downloadLink = obj.value("windows_url").toString();
#elif defined Q_OS_MACOS
			auto downloadLink = obj.value("mac_url").toString();
#else Q_OS_LINUX
			auto downloadLink = obj.value("linux_url").toString();
#endif


			emit updateNeeded(nextVersion, versionNotes, downloadLink);
		});
	}
}

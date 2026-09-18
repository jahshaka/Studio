/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "app/updatechecker.h"
#include "data/constants.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

const char *UpdateChecker::kAutomaticChecksKey = "automatic_updates";

UpdateChecker::UpdateChecker()
{
}

bool UpdateChecker::checkForAppUpdateIfEnabled(const QVariant &storedPreference)
{
	// NOTHING LEAVES THE MACHINE UNLESS THE USER ASKED (owner, 2026-09-18).
	// An absent preference is the default, and the default is OFF: an invalid
	// QVariant converts to false, so "never set" and "set to off" answer the
	// same — which is what a caller passing kAutomaticChecksDefault means.
	return checkForUpdateIfEnabled(
	    storedPreference, QUrl(Constants::UPDATE_CHECK_URL + Constants::CONTENT_VERSION));
}

bool UpdateChecker::checkForUpdateIfEnabled(const QVariant &storedPreference, const QUrl &url,
                                            int transferTimeoutMs)
{
	// An absent preference is the default, and the default is OFF: an invalid
	// QVariant converts to false, so "never set" and "set to off" answer the
	// same — which is what a caller passing kAutomaticChecksDefault means.
	if (!storedPreference.toBool()) return false;
	checkForUpdate(url, transferTimeoutMs);
	return checkInFlight();
}

void UpdateChecker::checkForAppUpdate()
{
	checkForUpdate(QUrl(Constants::UPDATE_CHECK_URL + Constants::CONTENT_VERSION));
}

void UpdateChecker::checkForUpdate(const QUrl &url, int transferTimeoutMs)
{
	// ONE CHECK AT A TIME. The app makes exactly one per launch, so this is
	// belt: without it a second call would overwrite the handle to the first
	// and orphan a live reply — the very leak the deleteLater below closes.
	if (checkInFlight()) return;

	QNetworkRequest request(url);
	request.setTransferTimeout(transferTimeoutMs);   // see kTransferTimeoutMs

	reply = manager.get(request);
	if (!reply) {
		emit checkFailed(QNetworkReply::UnknownNetworkError,
		                 QStringLiteral("the request could not be posted"));
		return;
	}

	// `this` is the context object, so the connection dies with the checker;
	// the reply is captured raw because the lambda only ever runs as that
	// reply's own finished() handler.
	connect(reply, &QNetworkReply::finished, this, [this, r = reply.data()]() {
		// FIRST, unconditionally: every path below returns, and the reply used
		// to survive all of them — one orphan per launch, owned by the manager
		// until the process exited.
		r->deleteLater();

		if (r->error() != QNetworkReply::NoError) {
			// A timed-out or refused check is a NO-OP, never a dialog: the app
			// is running, the user is here, and the update server is not the
			// user's problem. One log line so "why no update prompt" is
			// answerable.
			qDebug() << "update check: no answer —" << r->errorString();
			emit checkFailed(r->error(), r->errorString());
			return;
		}

		const QJsonObject obj = QJsonDocument::fromJson(r->readAll()).object();
		if (!obj.value("should_update").toBool(false)) return;

		const QString nextVersion = obj.value("id").toString();
		const QString versionNotes = obj.value("notes").toString();

#ifdef Q_OS_WIN
		const QString downloadLink = obj.value("windows_url").toString();
#elif defined Q_OS_MACOS
		const QString downloadLink = obj.value("mac_url").toString();
#else   // Q_OS_LINUX
		const QString downloadLink = obj.value("linux_url").toString();
#endif

		emit updateNeeded(nextVersion, versionNotes, downloadLink);
	});
}

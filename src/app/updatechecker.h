/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QUrl>

/// Asks the release endpoint whether a newer build exists, once, at launch
/// (main.cpp calls checkForAppUpdate() after the window is up). It is
/// DECORATION: an answer raises the update dialog, and every other outcome —
/// no network, a black-holed route, a dead endpoint, a body that is not the
/// JSON we expect — is a quiet no-op. The user asked to run the editor, not to
/// be told the update server is down.
class UpdateChecker : public QObject
{
	Q_OBJECT

public:
	/// THE TRANSFER TIMEOUT, and why this number. This is a few hundred bytes
	/// of JSON: on any usable connection it lands in well under a second, and
	/// a transfer that has made no progress for fifteen seconds is not slow,
	/// it is gone. A plain QNetworkAccessManager applies NO transfer timeout
	/// at all (measured on Qt 6.10: transferTimeout() = 0; a GET against a
	/// listening, never-answering socket had not finished after 40 s — the
	/// 30 s constant is only what setTransferTimeout() uses when called with
	/// no argument), so without this line a launch behind a black-holing
	/// route keeps a socket, a reply and the manager's state alive for the
	/// LIFE OF THE PROCESS, never finishing.
	/// Fifteen seconds is ~30x the headroom a real answer needs and half the
	/// dead weight. It is a TRANSFER timeout (no progress for that long), not
	/// a deadline on a big download; this fetch has nothing big to download.
	static constexpr int kTransferTimeoutMs = 15000;

	UpdateChecker();
	void checkForAppUpdate();
	/// Runs one check against `url`. Asynchronous: it returns as soon as the
	/// request is posted, so the UI thread never waits for the network.
	void checkForUpdate(const QUrl &url, int transferTimeoutMs = kTransferTimeoutMs);
	/// True while a check is waiting for an answer. It goes false once the
	/// reply has finished AND been destroyed (deleteLater, so one event-loop
	/// turn later) — which is how a test proves the reply is not leaked.
	bool checkInFlight() const { return !reply.isNull(); }

signals:
	void updateNeeded(QString newVersion, QString notes, QString downloadLink);
	/// A check that ended without an answer. NOTHING in the app connects this
	/// — the failure is meant to be silent, and this exists so that "silent"
	/// is observable in a test instead of indistinguishable from "no update".
	void checkFailed(QNetworkReply::NetworkError error, QString reason);

private:
	QNetworkAccessManager manager;
	QPointer<QNetworkReply> reply;
};

#endif // UPDATECHECKER_H

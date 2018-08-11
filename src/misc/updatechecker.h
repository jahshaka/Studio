/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>

class UpdateChecker : public QObject
{
	Q_OBJECT

	QNetworkAccessManager manager;
	QString currentVersion;
public:
	UpdateChecker();
	void checkForUpdate();
    void checkForPlayerUpdate();
    void checkForAppUpdate();
    void checkForMinerUpdate();
private :
    QUrl url;    
	QString type;

signals:
	void updateNeeded(QString newVersion, QString notes, QString downloadLink);
};

#endif UPDATECHECKER_H

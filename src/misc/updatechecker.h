#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QNetworkAccessManager>

class UpdateChecker : public QObject
{
	Q_OBJECT

	QNetworkAccessManager manager;
	QString currentVersion;
public:
	UpdateChecker();
	void checkForUpdate();

signals:
	void updateNeeded(QString newVersion, QString notes, QString downloadLink);
};

#endif UPDATECHECKER_H
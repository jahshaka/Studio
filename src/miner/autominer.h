#pragma once
#include <QObject>

class MinerUI;
class QElapsedTimer;
class QTimer;
class AutoMining : public QObject
{
	Q_OBJECT
	MinerUI* minerUI;
	float inactiveTime;
	QElapsedTimer* elapsedTimer;
	QTimer* timer;

	// this tracks whether mining was started from here
	bool isAutoMining;

public:
	AutoMining(MinerUI* minerUI);
	bool eventFilter(QObject* target, QEvent* evt);

private slots:
	void onTimer();
};
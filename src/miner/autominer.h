#pragma once
#include <QObject>

class MinerUI;
<<<<<<< HEAD
class QElapsedTimer;
class QTimer;
=======
>>>>>>> Added class for handling automatic mining
class AutoMining : public QObject
{
	Q_OBJECT
	MinerUI* minerUI;
	float inactiveTime;
<<<<<<< HEAD
	QElapsedTimer* elapsedTimer;
	QTimer* timer;

	// this tracks whether mining was started from here
	bool isAutoMining;
=======
>>>>>>> Added class for handling automatic mining

public:
	AutoMining(MinerUI* minerUI);
	bool eventFilter(QObject* target, QEvent* evt);
<<<<<<< HEAD

private slots:
	void onTimer();
=======
>>>>>>> Added class for handling automatic mining
};
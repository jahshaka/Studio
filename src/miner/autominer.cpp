#include "autominer.h"
<<<<<<< HEAD
#include "minerui.h"
#include <QElapsedTimer>
#include <QTimer>
=======

>>>>>>> Added class for handling automatic mining
#include <QEvent>

AutoMining::AutoMining(MinerUI* minerUI)
{
	this->minerUI = minerUI;
	inactiveTime = 0;
<<<<<<< HEAD
	isAutoMining = false;

	elapsedTimer = new QElapsedTimer();
	//connect(timer,SIGNAL(QElapsedTimer::))
	timer = new QTimer();
	connect(timer, SIGNAL(QTimer::timeout()), this, SLOT(timerEvent()));
	timer->start(1000 * 60);//timeout every minute

=======
>>>>>>> Added class for handling automatic mining
}

bool AutoMining::eventFilter(QObject* target, QEvent* evt)
{
	switch (evt->type()) {
<<<<<<< HEAD

	// mousemove should be enough for now
	case QEvent::MouseMove:

		if (isAutoMining) {
			minerUI->stopMining();
			isAutoMining = false;
		}
		inactiveTime = 0;
=======
	case QEvent::MouseMove:
>>>>>>> Added class for handling automatic mining
		break;
	}

	return QObject::eventFilter(target, evt);
<<<<<<< HEAD
}

void AutoMining::onTimer()
{
	inactiveTime += 1.0f;

	// hasnt been active in 15 mins, mining should start now
	if (inactiveTime > 15 &&
		minerUI->shouldAutoMine() == true && // automining should be turned off
		minerUI->isMining() == false) // shouldnt already be mining
	{
		minerUI->startMining();
		isAutoMining = true;
	}
=======
>>>>>>> Added class for handling automatic mining
}
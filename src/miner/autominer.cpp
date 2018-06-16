#include "autominer.h"

#include <QEvent>

AutoMining::AutoMining(MinerUI* minerUI)
{
	this->minerUI = minerUI;
	inactiveTime = 0;
}

bool AutoMining::eventFilter(QObject* target, QEvent* evt)
{
	switch (evt->type()) {
	case QEvent::MouseMove:
		break;
	}

	return QObject::eventFilter(target, evt);
}
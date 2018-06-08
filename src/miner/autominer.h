#pragma once
#include <QObject>

class MinerUI;
class AutoMining : public QObject
{
	Q_OBJECT
	MinerUI* minerUI;
	float inactiveTime;

public:
	AutoMining(MinerUI* minerUI);
	bool eventFilter(QObject* target, QEvent* evt);
};
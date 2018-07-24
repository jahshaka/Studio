/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MINERUI_H
#define MINERUI_H

#include <QScrollArea>
#include <QLayout>
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPainter>
#include <QStyleOption>
#include <QPushButton>
#include <QToolBar>
#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include <QStackedWidget>
#include <QComboBox>
#include "mswitch.h"
#include "../misc/QtAwesome.h"
#include "minerprocess.h"
#include "minerchart.h"

#include "../src/subclass/switch.h"


class QtAwesome;
class Dot : public QWidget {

	Q_OBJECT
public:
        Dot(QColor col, QWidget *parent = Q_NULLPTR);
        void setColor(QColor col);
protected:
        void paintEvent(QPaintEvent *event);
private:
	QColor color;
};

class Switch;
class GraphicsCardUI : public QWidget
{
	Q_OBJECT
public:

	enum class MinerConnection {
		Connected = 1,
		Connecting = 2,
		Notconnected = 3,
		Inactive = 4
	};

	
        GraphicsCardUI(QWidget *parent = Q_NULLPTR);

        void setCardName(QString name);
        void expand();
        void contract();
        void setSpeed(double rate);
        void setArmed(bool armed);
        void setDotColor(MinerConnection con);
        void hideLabel();
        void showLabel();
        void setStarted(bool val);
        void setHighlight(bool val);
        void setMinerProcess(MinerProcess* process);
        void startMining();
        void stopMining();
private:
	MinerChart * info;
	QWidget *additional;
	QColor color;
	QLabel *cardName, *pool, *speed, *displayLabel;
	Switch *switchBtn;
	Dot *dot;
	QPushButton *logo;
	QString oldString;
	bool armed=false, mining=false;
	MinerProcess* process;

	void setColor(MinerConnection status);
    void configureCard();
    void configureConnections();

signals:
	void switchIsOn(bool);

};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MinerManager;
class SettingsManager;
class MinerUI : public QWidget
{
	Q_OBJECT
public:
	MinerUI(QWidget *parent = 0);
	~MinerUI();
	GraphicsCardUI* addGraphicsCard(QString string);
	void setToStartAutomatically(bool shouldAuto) {
		startAutomatically = shouldAuto;
	}

	bool shouldAutoMine()
	{
		return startAutomatically;
	}


	bool isMining()
	{
		return mining;
	}

private slots:
	void switchToAdvanceMode();

private:
	void configureUI();
	void configureSettings();
	void configureConnections();
	void configureStyleSheet();

	bool isInAdvanceMode = false, startAutomatically, mining=false;
	bool isPressed = false;
	QStackedWidget *stack;
	QList<GraphicsCardUI *> list;
	QToolBar *toolbar;
	QGridLayout *layout;
	QVBoxLayout *cardHolderLayout;
	QScrollArea *scrollArea;
	QWidget *cardHolder;
	GraphicsCardUI *card;
	QPushButton *startBtn;
	QLabel *coinType, *autostart;
	QAction *settings, *close;
	QAction *advance;
	Switch *autoStartSwitch;
	QAction *back;
	QComboBox *currency;
	QLineEdit *walletEdit, *passwordEdit, *poolEdit, *identifierEdit;
	QtAwesome fontIcon;

	QPoint oldPos;
	MinerManager* minerMan;
	SettingsManager* settingsMan;

	QString walletIdText;
	QString poolText;
	QString passwordText;
	QString identifierText;


protected:
	void mousePressEvent(QMouseEvent *event) {
		isPressed = true;

		oldPos = event->globalPos();
	}

	void mouseMoveEvent(QMouseEvent *event) {
		QPoint delta = event->globalPos() - oldPos;
		if (isPressed) move(x() + delta.x(), y() + delta.y());
		oldPos = event->globalPos();
	}

	void mouseReleaseEvent(QMouseEvent *event) {
		isPressed = false;

	}
	
	void saveAndApplySettings();
	void restoreSettings();


public:

	void restartMining();
	void startMining();
	void stopMining();

};

#endif // MINERUI_H

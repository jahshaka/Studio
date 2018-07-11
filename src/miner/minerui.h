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


class QtAwesome;
class Dot : public QWidget {

	Q_OBJECT
public:
	Dot(QColor col, QWidget *parent = Q_NULLPTR) :QWidget(parent) {
		setFixedSize(15, 15);
		color = col;
	}
	void setColor(QColor col) {
		color = col;
		repaint();
	}

protected:
	void paintEvent(QPaintEvent *event) {
		Q_UNUSED(event);
		QPainter painter(this);
		painter.setRenderHint(QPainter::HighQualityAntialiasing);
		painter.setPen(QPen(color, 6));
		painter.drawEllipse(3, 4, 7, 7);
	}
private:
	QColor color;
};

class MSwitch;
class GraphicsCardUI : public QWidget
{
	Q_OBJECT
public:
	GraphicsCardUI(QWidget *parent = Q_NULLPTR) : QWidget(parent) {

		configureCard();
		configureConnections();
		setColor((int)Connection::NOTCONNECTED);
		contract();

	}

	void setCardName(QString name) {
		cardName->setText(name);
		repaint();
	}

	void expand() {
		additional->show();
	}

	void contract() {
		additional->hide();
	}

	void setSpeed(double rate) {
		speed->setText("Speed: " + QString::number(rate) +" H/s");
	}

	void setArmed(bool armed) {
		this->armed = armed;
	}

	void setDotColor(int con) {
		setColor(con);
		dot->setColor(color);
	}

	void hideLabel() {
		displayLabel->hide();
	}

	void showLabel() {
		displayLabel->show();
	}

	void setStarted(bool val) {
		if (armed && val)
		{
			mining = val;
			if (process != nullptr) {
				process->startMining();
				setHighlight(true);
			}
		}
		else {
			if (process != nullptr) {
				if (process->isMining()) {
					process->stopMining();
					setHighlight(false);
				}
			}
		}
	}

	void setHighlight(bool val) {
		logo->setCheckable(true);
		logo->setChecked(val);
		displayLabel->setText(val? "--Mining--": oldString);		
	}

	void setMinerProcess(MinerProcess* process)
	{
		this->process = process;

		if (process != nullptr) {
			connect(process, &MinerProcess::onMinerChartData, [this](MinerChartData data)
			{
				// set last hash to ui
				this->setSpeed(data.hps);

				// if hps is 0 then it must be connecting
				// set pool color to orange
				if (data.connected)
					this->setDotColor((int)Connection::CONNECTED);
				else
					this->setDotColor((int)Connection::CONNECTING);

				if (data.hps != 0) {
					if (this->info->data.size() > 100)
						this->info->data.removeFirst();
					this->info->data.append(data);
					this->info->repaint();
				}
			});

			connect(process, &MinerProcess::minerStatusChanged, [this](MinerStatus status)
			{
				switch (status)
				{
				case MinerStatus::Idle:
					this->setDotColor((int)Connection::INACTIVE);
					break;
				case MinerStatus::Starting:
					this->setDotColor((int)Connection::CONNECTING);
					break;
				case MinerStatus::Mining:
					this->setDotColor((int)Connection::CONNECTED);
					break;
				case MinerStatus::Stopping:
					this->setDotColor((int)Connection::NOTCONNECTED);
					break;
				}
			});
		}
	}

	void startMining()
	{
		if (armed) {
			process->startMining();
		}
	}

	void stopMining()
	{
		process->stopMining();
	}


private:
	MinerChart * info;
	QWidget *additional;
	QColor color;
	QLabel *cardName, *pool, *speed, *displayLabel;
	MSwitch *switchBtn;
	Dot *dot;
	QPushButton *logo;
	QString oldString;
	bool armed=false, mining=false;
	MinerProcess* process;

	enum class Connection {
		CONNECTED = 1,
		CONNECTING = 2,
		NOTCONNECTED = 3,
		INACTIVE = 4
	};

	void setColor(int num) {
		switch (num) {
		case 1:
			color.setRgb(0, 120, 0, 255);
			break;
		case 2:
			color.setRgb(255, 120, 70, 255);
			break;
		case 3:
			color.setRgb(170, 1, 2, 255);
			break;
		case 4:
			color.setRgb(240, 240, 240, 255);
			break;
		}

	}

	void configureCard() {

		

		// QVBoxLayout *cardLayout = new QVBoxLayout;
		QHBoxLayout *mainLayout = new QHBoxLayout;
		QHBoxLayout *sliderLayout = new QHBoxLayout;
		QVBoxLayout *logoLayout = new QVBoxLayout;
		QVBoxLayout *infoLayout = new QVBoxLayout;
		auto poolDotLayout = new QHBoxLayout;

		auto card = new QWidget;
		auto cardLayout = new QGridLayout;
		card->setLayout(cardLayout);
		card->setObjectName(QStringLiteral("card"));
		card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		auto toolBar = new QWidget;
		toolBar->setObjectName(QStringLiteral("cardBar"));
		auto toolBarLayout = new QHBoxLayout;
		toolBar->setLayout(toolBarLayout);
		toolBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		logo = new QPushButton();
		logo->setObjectName(QStringLiteral("logo"));
		logo->setLayout(logoLayout);
		logo->setFixedSize(150, 118);
		//logo->setCheckable(true);
		//logo->setCursor(Qt::PointingHandCursor);
		logo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
		//logo->blockSignals(true);


		pool = new QLabel("Pool  ");
		pool->setAlignment(Qt::AlignLeft);
		dot = new Dot(color);
		poolDotLayout->addWidget(pool);
		poolDotLayout->addWidget(dot);
		poolDotLayout->setSpacing(2);
		setDotColor((int)Connection::CONNECTED);

		speed = new QLabel("Speed: ");
		speed->setAlignment(Qt::AlignLeft);
		cardName = new QLabel("AMD A9");
		cardName->setAlignment(Qt::AlignHCenter);
		cardName->setWordWrap(true);
		cardName->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

		QFont font = cardName->font();
		font.setStyleStrategy(QFont::PreferAntialias);
		//font.setBold(true);
		cardName->setFont(font);
		font.setBold(false);
		font.setPixelSize(12);
		pool->setFont(font);
		speed->setFont(font);

		switchBtn = new MSwitch();
		switchBtn->setColor(QColor(40, 128, 185));
		switchBtn->setSizeOfSwitch(22);
		sliderLayout->addStretch();
		sliderLayout->addWidget(switchBtn);
		sliderLayout->addStretch();


		//logoLayout->addStretch();
		logoLayout->addWidget(cardName);
		logoLayout->addLayout(poolDotLayout);
		logoLayout->addWidget(speed);
		logoLayout->addLayout(sliderLayout);
		//logoLayout->addStretch();

		//info = new QWidget();
		info = new MinerChart();
		info->setObjectName(QStringLiteral("info"));
		info->setLayout(infoLayout);
		info->setFixedHeight(98);
		info->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		additional = new QWidget();
		additional->setObjectName(QStringLiteral("additional"));
		additional->setMinimumHeight(80);
		additional->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

		displayLabel = new QLabel("GPU is not set to mine");
		displayLabel->setAlignment(Qt::AlignHCenter);
		displayLabel->setObjectName(QStringLiteral("gpuLabel"));
		font.setBold(true);
		displayLabel->setFont(font);
		infoLayout->addWidget(displayLabel);
		oldString = displayLabel->text();

		toolBarLayout->addWidget(logo);
		toolBarLayout->addWidget(info);

		cardLayout->addWidget(toolBar);
		cardLayout->addWidget(additional);

		// mainLayout->addWidget(logo);
		// mainLayout->addWidget(info);
		//        cardLayout->addLayout(mainLayout);
		//        cardLayout->addWidget(additional);
		//        cardLayout->setSpacing(6);
		//cardLayout->setSizeConstraint(QLayout::SetFixedSize);
		cardLayout->setContentsMargins(2, 1, 3, 2);
		setLayout(mainLayout);
		mainLayout->addWidget(card);
		
		card->setMinimumHeight(150);
		card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

		//qDebug() << card->geometry();
		setStyleSheet(" * {color: white; }"
			"QWidget#logo, QWidget#info,QWidget#additional  { background:rgba(17,17,17,0); border : 0px solid rgba(00,00,00,.2); border-radius: 1px; margin: 0px;  }"
			//"QWidget#logo:hover, QWidget#info:hover,QWidget#additional:hover{border : 1px solid rgba(40,128,185,.01); }"
			//"QLabel{ color:rgba(255,255,255,.8); padding :3px; }"
			"QWidget#info  { border-left : 5px solid rgba(00,00,00,.1);  }"
			"#card:hover{background: rgba(40,128,185,.11); }"
			"#logo:checked {background: qlineargradient(x1: 0, y1: 1, x2: 1, y2: 1, stop: 0 rgba(40,128,185,.5), stop: 0.5 rgba(17,17,17,0));}"
			"QToolBar::separator{ background:rgba(0,0,0,.1);}"
			"#card{background: rgba(40,128,185,.1); border: 1px solid rgba(0,0,0,.85);}"
			"#gpuLabel{ color: rgba(150,150,170,.8);}");

		QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect;
		effect->setBlurRadius(10);
		effect->setXOffset(0);
		effect->setYOffset(0);
		effect->setColor(QColor(0, 0, 0));
		//  card->setGraphicsEffect(effect);
		//additional->setGraphicsEffect(effect);
		//logo->setGraphicsEffect(effect);
	}

	void configureConnections() {

		connect(switchBtn, &MSwitch::switchPressed, [this](bool val) {
			emit switchIsOn(val);
			armed = val;
		//	logo->setChecked(val);
			displayLabel->setText(val ? "GPU set to mined" : "GPU is not set to mine");
			oldString = displayLabel->text();
		});

		//connect(logo, &QPushButton::clicked, [this]() {
		//	emit switchIsOn(logo->isChecked());
		//	armed = logo->isChecked();
		//	if (logo->isChecked()) {
		//	//	displayLabel->setText("GPU set to mined");
		//		switchBtn->toggle();

		//	}
		//	else {
		//	//	displayLabel->setText("GPU is not set to mine");
		//		switchBtn->toggle();

		//	}			
		//});

		connect(logo, &QPushButton::clicked, [=]() {
			logo->setChecked(false);
			switchBtn->toggle();
		});


	}


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
	MSwitch *autoStartSwitch;
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

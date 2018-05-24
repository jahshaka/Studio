#ifndef MINERUI_H
#define MINERUI_H

#include <QScrollArea>
#include <QLayout>
#include <QWidget>
#include <QLabel>
#include <QSlider>
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
#include "QtAwesome.h"


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
		setColor(1);

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

	void setsSpeed(double rate) {
		speed->setText("Speed: " + QString::number(rate));
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

	void setStarted() {
		if (mining) {
			mining = false;

		}

	}


private:
	QWidget * info, *additional;
	QColor color;
	QLabel *cardName, *pool, *speed, *displayLabel;
	MSwitch *switchBtn;
	Dot *dot;
	QPushButton *logo;
	bool armed, mining;

	enum Connection {
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

		auto toolBar = new QToolBar;
		toolBar->setFixedWidth(450);

		logo = new QPushButton();
		logo->setObjectName(QStringLiteral("logo"));
		logo->setLayout(logoLayout);
		logo->setFixedSize(100, 98);
		logo->setCheckable(true);
		logo->setCursor(Qt::PointingHandCursor);


		pool = new QLabel("Pool  ");
		pool->setAlignment(Qt::AlignHCenter);
		dot = new Dot(color);
		poolDotLayout->addWidget(pool);
		poolDotLayout->addWidget(dot);
		poolDotLayout->setSpacing(2);
		setDotColor(CONNECTED);

		speed = new QLabel("Speed: ");
		speed->setAlignment(Qt::AlignHCenter);
		cardName = new QLabel("AMD A9");
		cardName->setAlignment(Qt::AlignHCenter);
		cardName->setWordWrap(true);

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
		// logoLayout->addLayout(sliderLayout);
		//logoLayout->addStretch();

		info = new QWidget();
		info->setObjectName(QStringLiteral("info"));
		info->setLayout(infoLayout);
		info->setFixedHeight(98);
		info->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		additional = new QWidget();
		additional->setObjectName(QStringLiteral("additional"));
		additional->setMinimumHeight(80);
		additional->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

		displayLabel = new QLabel("GPU is not set to mined");
		displayLabel->setAlignment(Qt::AlignHCenter);
		displayLabel->setObjectName(QStringLiteral("gpuLabel"));
		font.setBold(true);
		displayLabel->setFont(font);
		infoLayout->addWidget(displayLabel);

		toolBar->addWidget(logo);
		toolBar->addSeparator();
		toolBar->addWidget(info);

		cardLayout->addWidget(toolBar);
		cardLayout->addWidget(additional);

		// mainLayout->addWidget(logo);
		// mainLayout->addWidget(info);
		//        cardLayout->addLayout(mainLayout);
		//        cardLayout->addWidget(additional);
		//        cardLayout->setSpacing(6);
		cardLayout->setSizeConstraint(QLayout::SetFixedSize);
		cardLayout->setContentsMargins(2, 1, 3, 2);
		setLayout(mainLayout);
		mainLayout->addWidget(card);

		qDebug() << card->geometry();
		setStyleSheet(" QWidget#logo, QWidget#info,QWidget#additional  { background:rgba(17,17,17,0); border : 0px solid rgba(00,00,00,.2); border-radius: 1px; margin: 0px;  }"
			"QWidget#logo:hover, QWidget#info:hover,QWidget#additional:hover{border : 1px solid rgba(40,128,185,.01); }"
			"QLabel{ color:rgba(255,255,255,.8); padding :3px; }"
			"#logo:checked {background: qlineargradient(x1: 0, y1: 1, x2: 1, y2: 1, stop: 0 rgba(40,128,185,.5), stop: 0.5 rgba(17,17,17,0));}"
			"QToolBar::separator{ background:rgba(0,0,0,.1);}"
			"#card{background: rgba(40,128,185,.1); border: 3px solid rgba(0,0,0,.85);}"
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
		});

		connect(logo, &QPushButton::clicked, [this]() {
			emit switchIsOn(logo->isChecked());
			armed = logo->isChecked();
			if (logo->isChecked())  displayLabel->setText("GPU set to mined");
			else displayLabel->setText("GPU is not set to mine");
		});
	}

signals:
	void switchIsOn(bool);

};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



class MinerUI : public QWidget
{
	Q_OBJECT
public:
	MinerUI(QWidget *parent = 0);
	~MinerUI();
	void addGraphicsCard(QString string);
	bool setToStartAutomatically() {
		return startAutomatically;
	}
	private slots:
	void switchToAdvanceMode();

private:
	void configureUI();
	void configureSettings();
	void configureConnections();
	void configureStyleSheet();

	bool isInAdvanceMode = true, startAutomatically, mining;
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
	QPushButton *back;
	QComboBox *currency;
	QtAwesome fontIcon;
};

#endif // MINERUI_H

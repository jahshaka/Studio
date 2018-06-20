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
        Dot(QColor col, QWidget *parent = Q_NULLPTR);
        void setColor(QColor col);
protected:
        void paintEvent(QPaintEvent *event);
private:
	QColor color;
};

class MSwitch;
class GraphicsCardUI : public QWidget
{
	Q_OBJECT
public:
        GraphicsCardUI(QWidget *parent = Q_NULLPTR);

        void setCardName(QString name);
        void expand();
        void contract();
        void setSpeed(double rate);
        void setArmed(bool armed);
        void setDotColor(int con);
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

        void setColor(int num);

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

	void restartMining();
	void startMining();
	void stopMining();

};

#endif // MINERUI_H

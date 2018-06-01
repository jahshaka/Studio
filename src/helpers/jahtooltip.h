#ifndef JAHTOOLTIP_H
#define JAHTOOLTIP_H

#include <QEvent>
#include <QMainWindow>
#include <QWidget>

class JahToolTip : public QWidget
{
	Q_OBJECT
public:
	explicit JahToolTip(QWidget *parent = nullptr);
	static void showToolTip(QWidget *sender);
	static void morphToolTip(QWidget *sender);
	static QWidget* getSender();
	static bool isShowing, isFading, isActive;


private:
	static JahToolTip *instance;
	QTimer *timer;
	static bool morphing;
	static QWidget *senderObj;
	static void setLocation();
	void hideImmediately();
	static void fadeIn();
	static void fadeOut();

protected:
	bool eventFilter(QObject *watched, QEvent *event);
	void enterEvent(QEvent *event);
	void leaveEvent(QEvent *event);
	void mousePressEvent(QEvent *event);

signals:
	void finishedFadingIn();
	void finishedFadingOut();
	void locationChanged(QPoint point);

	public slots:
};


class JahToolTipHelper : public QWidget
{
	Q_OBJECT

public:
	bool eventFilter(QObject *watched, QEvent *event);
};

#endif // JAHTOOLTIP_H

#ifndef TOOLTIP_H
#define TOOLTIP_H

#include <QEvent>
#include <QMainWindow>
#include <QWidget>

class ToolTip : public QWidget
{
	Q_OBJECT
public:
	explicit ToolTip(QWidget *parent = nullptr);
	static void showToolTip(QWidget *sender);
	static void morphToolTip(QWidget *sender);
	static QWidget* getSender();
	static bool isShowing, isFading, isActive;

private:
	static ToolTip *instance;
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

class ToolTipHelper : public QWidget
{
	Q_OBJECT
public:
	bool eventFilter(QObject *watched, QEvent *event);
};

#endif // TOOLTIP_H
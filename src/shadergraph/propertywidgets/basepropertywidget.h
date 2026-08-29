#pragma once
#include <QWidget>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include "../models/properties.h"
#include "../misc/QtAwesome.h"
#include "../misc/QtAwesomeAnim.h"

class HeaderObject : public QWidget
{
	Q_OBJECT
public:
	HeaderObject();

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event);
	void enterEvent(QEvent *event);
};


class WideRangeSpinBox : public QDoubleSpinBox
{
public:
	WideRangeSpinBox(QWidget *parent = Q_NULLPTR);
};


class WideRangeIntBox : public QSpinBox
{
public:
	WideRangeIntBox(QWidget *parent = Q_NULLPTR);
};


class BasePropertyWidget : public QWidget
{
	Q_OBJECT
public:
	BasePropertyWidget(QWidget *parent = nullptr);
	~BasePropertyWidget();

	QLineEdit *displayName;
	HeaderObject *displayWidget;
	QWidget *contentWidget = 0;
	QVBoxLayout *layout;
	Property *modelProperty;
	QPropertyAnimation *anim;

	bool pressed = false;
	bool minimized = false;
	int index = 0;

	void setWidget(QWidget *widget);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;


private:
	QPushButton *button;
	QSize currentSize;
	QtAwesome *fontIcons;

signals:
	void buttonPressed(bool shouldDelete);
	void buttonClose();
	void buttonResize(bool resize);
	void currentWidget(BasePropertyWidget *widget);
	void shouldSetVisible(bool val);
	void TitleChanged(QString title);
	//void requestMimeData(Property *prop);
};


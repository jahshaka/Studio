#pragma once

#include "customdialog.h"

class Triangle : public QWidget
{
public:
	Triangle(QPoint point, QWidget *parent = Q_NULLPTR);
private:
	QPoint points[3];
	int dist = 20;
protected:
	void paintEvent(QPaintEvent *event) override;
};

class CustomPopup : public CustomDialog
{
public:
	CustomPopup(QPoint point, Qt::Edge ori = Qt::TopEdge);
	~CustomPopup();

	void addConfirmButton(QString text = "ok");
	void addCancelButton(QString text = "cancel");
private:
	void calculatePoints(Qt::Edge side);
};


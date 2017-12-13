#ifndef ASSETGRIDITEM_HPP
#define ASSETGRIDITEM_HPP

#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>
#include <QResizeEvent>
#include <QGridLayout>
#include <QScrollArea>
#include <qdebug.h>
#include <QLabel>
#include <QButtonGroup>

#include <QWidget>

#include "irisgl/src/core/irisutils.h"

class AssetGridItem : public QWidget
{
	Q_OBJECT

public:
	QString url;
	QPixmap pixmap;
	QLabel *gridImageLabel;
	QLabel *textLabel;
	bool selected;
	QJsonObject metadata;

	AssetGridItem() = default;
	AssetGridItem(QJsonObject details, QImage image, QWidget *parent = Q_NULLPTR);
	void setTile(QPixmap pix);
	void highlight(bool);
	
	void enterEvent(QEvent *event);
	void leaveEvent(QEvent *event);
	void mousePressEvent(QMouseEvent *event);

public slots:
	void dimHighlight();
	void noHighlight();

signals:
	void hovered();
	void left();
	void singleClicked(AssetGridItem*);
};

#endif // ASSETGRIDITEM_HPP
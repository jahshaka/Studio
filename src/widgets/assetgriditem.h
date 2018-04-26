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
	QJsonObject sceneProperties;
	QJsonObject tags;

	AssetGridItem() = default;
	AssetGridItem(QJsonObject details, QImage image, QJsonObject properties, QJsonObject tags, QWidget *parent = Q_NULLPTR);
	void setTile(QPixmap pix);
	void highlight(bool);
	void updateMetadata(QJsonObject details, QJsonObject tags);
	
	void enterEvent(QEvent *event);
	void leaveEvent(QEvent *event);
	void mousePressEvent(QMouseEvent *event);

public slots:
	void projectContextMenu(const QPoint &pos);
	void dimHighlight();
	void noHighlight();

signals:
	void hovered();
	void left();
	void singleClicked(AssetGridItem*);
	void specialClicked(AssetGridItem*);	// bypass loading asset and add to scene
	void contextClicked(AssetGridItem*);	// use this exclusively for right clicks

	void addAssetToProject(AssetGridItem*);
	void addAssetItemToProject(AssetGridItem*);
	void changeAssetCollection(AssetGridItem*);
	void removeAssetFromProject(AssetGridItem*);
};

#endif // ASSETGRIDITEM_HPP
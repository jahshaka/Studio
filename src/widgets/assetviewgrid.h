#ifndef ASSETVIEWGRID_HPP
#define ASSETVIEWGRID_HPP

#include <QScrollArea>
#include <QGridLayout>
#include <QJsonObject>
#include <QResizeEvent>

class AssetGridItem;

class AssetViewGrid : public QScrollArea
{
	Q_OBJECT

public:
	QGridLayout *_layout;
	int lastWidth;
	QWidget *parent;

	AssetViewGrid(QWidget *parent);

	void updateImage();

	void addTo(QJsonObject details, QImage image, int count);
	void resizeEvent(QResizeEvent *event);
	void mousePressEvent(QMouseEvent*);
	void updateGridColumns(int width);
	void deselectAll();

private:
	int gridCounter;
	QWidget *gridWidget;
	QList<AssetGridItem*> originalItems;

signals:
	void gridCount(int);
    void selectedTile(AssetGridItem*);
};

#endif // ASSETVIEWGRID_HPP
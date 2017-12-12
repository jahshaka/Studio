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
	QGridLayout *fastGrid;
	int lastWidth;
	QWidget *parent;

	AssetViewGrid(QWidget *parent);

	void updateImage();

	void addTo(QJsonObject details, QImage image, int count);
	void resizeEvent(QResizeEvent *event);
	void updateGridColumns(int width);

private:
	QWidget *gridWidget;

signals:
	void gridCount(int);
    void selectedTile(AssetGridItem*);
};

#endif // ASSETVIEWGRID_HPP
#ifndef DYNAMICGRID_H
#define DYNAMICGRID_H

#include <QScrollArea>

class QGridLayout;
class GridWidget;
class SettingsManager;
class ItemGridWidget;

class DynamicGrid : public QScrollArea
{
    Q_OBJECT

public:
    explicit DynamicGrid(QWidget *parent = Q_NULLPTR);
    void addToGridView(GridWidget *item, int count);
    QSize tileSize;
    QSize baseSize;
    int lastWidth;
    QList<ItemGridWidget*> originalItems;
    int scale;
    int offset;
    float scl;
    void scaleTile(QString);
    void searchTiles(QString);

    void deleteTile(ItemGridWidget*);

    void resetView();

protected:
    void resizeEvent(QResizeEvent *event);

private:
    void updateGridColumns(int width);
    int autoColumnCount;
    QSize sizeFromString(QString);

    QWidget *parent;
    QWidget *gridWidget;
    QGridLayout *gridLayout;
    SettingsManager *settings;
};

#endif // DYNAMICGRID_H

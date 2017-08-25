#ifndef DYNAMICGRID_H
#define DYNAMICGRID_H

#include <QScrollArea>

class QGridLayout;
class GridWidget;

class DynamicGrid : public QScrollArea
{
    Q_OBJECT

public:
    explicit DynamicGrid(QWidget *parent = Q_NULLPTR);
    void addToGridView(GridWidget *item, int count);
    QSize tileSize;
    QSize baseSize;
    int lastWidth;
    int scale;
    void scaleTile(int);

    void resetView();

protected:
    void resizeEvent(QResizeEvent *event);

private:
    void updateGridColumns(int width);
    int autoColumnCount;

    QWidget *parent;
    QWidget *gridWidget;
    QGridLayout *gridLayout;
};

#endif // DYNAMICGRID_H

#ifndef ITEMGRIDWIDGET_HPP
#define ITEMGRIDWIDGET_HPP

#include <QWidget>
#include <QLabel>
#include <QGridLayout>

class GridWidget;

class ItemGridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ItemGridWidget(GridWidget *item, QSize size, QWidget *parent = Q_NULLPTR);
    QSize tileSize;

    void setTileSize(QSize size);
    void updateImage();

    QWidget *get();

private:
    QWidget *gameGridItem;
    QGridLayout *gameGridLayout;
    QLabel *gridImageLabel;
    QPixmap image;
    QPixmap oimage;
    QWidget *parent;
};

#endif // ITEMGRIDWIDGET_HPP

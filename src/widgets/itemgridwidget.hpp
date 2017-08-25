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
    QString projectName;

    void setTileSize(QSize size);
    void updateImage();

    QWidget *get();

protected:
//    void keyPressEvent(QKeyEvent* event);
    void mousePressEvent(QMouseEvent *event);
    void mouseDoubleClickEvent(QMouseEvent *event);

signals:
//    void arrowPressed(QWidget *current, QString keypress);
//    void enterPressed(QWidget *current);
    void singleClicked(ItemGridWidget *current);
    void doubleClicked(ItemGridWidget *current);

private:
//    QWidget *gameGridItem;
    QWidget *options;
    QGridLayout *gameGridLayout;
    QLabel *gridImageLabel;
    QLabel *gridTextLabel;
    QPixmap image;
    QPixmap oimage;
    QWidget *parent;
};

#endif // ITEMGRIDWIDGET_HPP

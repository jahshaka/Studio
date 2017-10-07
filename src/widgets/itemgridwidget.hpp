#ifndef ITEMGRIDWIDGET_HPP
#define ITEMGRIDWIDGET_HPP

#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QLineEdit>

class GridWidget;

class ItemGridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ItemGridWidget(GridWidget *item, QSize size, QWidget *parent = Q_NULLPTR);
    QSize tileSize;
    QString projectName;
    QString name;
    QString guid;

    void setTileSize(QSize size);
    void updateImage();
    void updateLabel(QString);
    QString labelText;

protected slots:
    void showControls();
    void hideControls();
    void removeProject();
    void editProject();
    void projectContextMenu(const QPoint&);

    void playProject();
    void openProject();
    void exportProject();
    void renameProject();
    void closeProject();
    void deleteProject();

    void renameFromWidgetStr(QString);

protected:
//    void keyPressEvent(QKeyEvent* event);
    void enterEvent(QEvent*);
    void leaveEvent(QEvent*);
    void mousePressEvent(QMouseEvent*);
    void mouseDoubleClickEvent(QMouseEvent*);

signals:
//    void arrowPressed(QWidget *current, QString keypress);
//    void enterPressed(QWidget *current);
    void hovered();
    void left();
    //void edit(ItemGridWidget*, bool playMode);
    void remove(ItemGridWidget*);
    void singleClicked(ItemGridWidget*);
    void doubleClicked(ItemGridWidget*);

    void openFromWidget(ItemGridWidget*, bool playMode);
    void exportFromWidget(ItemGridWidget*);
    void renameFromWidget(ItemGridWidget*);
    void closeFromWidget(ItemGridWidget*);
    void deleteFromWidget(ItemGridWidget*);

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

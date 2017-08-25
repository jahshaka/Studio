#ifndef GRIDWIDGET_H
#define GRIDWIDGET_H

#include <QWidget>

class GridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GridWidget(QString path, QWidget *parent = Q_NULLPTR);
    QPixmap image;
    QString path;
    QString projectName;
};

#endif // GRIDWIDGET_H

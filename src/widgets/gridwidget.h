#ifndef GRIDWIDGET_H
#define GRIDWIDGET_H

#include <QWidget>
#include "../core/project.h"

class GridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GridWidget(ProjectTileData tileData, QWidget *parent = Q_NULLPTR);
    QPixmap image;
    QString path;
    QString projectName;
};

#endif // GRIDWIDGET_H

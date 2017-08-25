#include "gridwidget.h"

#include <QFileInfo>
#include <QDebug>

GridWidget::GridWidget(QString path, QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);

    this->path = path;
    auto img = QFileInfo(path + "/Metadata/preview.png");

    if (img.exists()) {
        image = QPixmap::fromImage(QImage(img.absoluteFilePath()));
    } else {
        image = QPixmap::fromImage(QImage(":/images/preview.png"));
    }


    QFileInfo finfo = QFileInfo(path);
//    qDebug() << finfo.baseName();

    projectName = path + "/" + finfo.baseName() + ".jah";
}

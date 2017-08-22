#include "gridwidget.h"

GridWidget::GridWidget(QString path, QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    image = QPixmap::fromImage(QImage(path));
}

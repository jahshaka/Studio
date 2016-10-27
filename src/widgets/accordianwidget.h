#ifndef ACCORDIANWIDGET_H
#define ACCORDIANWIDGET_H

#include <QWidget>

namespace Ui {
class AccordianWidget;
}

class AccordianWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AccordianWidget(QWidget *parent = 0);
    ~AccordianWidget();

private:
    Ui::AccordianWidget *ui;
};

#endif // ACCORDIANWIDGET_H

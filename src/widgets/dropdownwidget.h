#ifndef DROPDOWNWIDGET_H
#define DROPDOWNWIDGET_H

#include <QWidget>

namespace Ui {
class DropDownWidget;
}

class DropDownWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DropDownWidget(QWidget *parent = 0);
    ~DropDownWidget();

private:
    Ui::DropDownWidget *ui;
};

#endif // DROPDOWNWIDGET_H

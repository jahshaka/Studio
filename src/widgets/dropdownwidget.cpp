#include "dropdownwidget.h"
#include "ui_dropdownwidget.h"

DropDownWidget::DropDownWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DropDownWidget)
{
    ui->setupUi(this);
}

DropDownWidget::~DropDownWidget()
{
    delete ui;
}

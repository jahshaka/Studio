#include "accordianwidget.h"
#include "ui_accordianwidget.h"

AccordianWidget::AccordianWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AccordianWidget)
{
    ui->setupUi(this);
}

AccordianWidget::~AccordianWidget()
{
    delete ui;
}

#include "propertywidget.h"
#include "ui_propertywidget.h"

PropertyWidget::PropertyWidget(QWidget *parent) : QWidget(parent), ui(new Ui::PropertyWidget)
{
    ui->setupUi(this);
}

PropertyWidget::~PropertyWidget()
{
    delete ui;
}

void PropertyWidget::setProperties(const QList<Property *> &properties)
{

}

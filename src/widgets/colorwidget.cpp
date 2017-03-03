#include "colorwidget.hpp"
#include "ui_colorwidget.h"

ColorWidget::ColorWidget(QWidget* parent) : QWidget(parent), ui(new Ui::ColorWidget)
{
    ui->setupUi(this);
    ui->color->setStyleSheet("background: green");
}

void ColorWidget::setLabel(QString label)
{
    this->ui->label->setText(label);
}

void ColorWidget::setColor(QColor color)
{

}

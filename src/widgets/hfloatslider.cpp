#include "hfloatslider.h"
#include "ui_hfloatslider.h"
#include <QDebug>

HFloatSlider::HFloatSlider(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::HFloatSlider)
{
    ui->setupUi(this);
 //   connect(ui->spinbox,SIGNAL(valueChanged(float)),ui->slider,SLOT(setValue(int)));
 //   connect(ui->slider,SIGNAL(valueChanged(int)),ui->spinbox,SLOT(setValue(float)));

}

HFloatSlider::~HFloatSlider()
{
    delete ui;
}

void  HFloatSlider::setValue( float value ){
   ui->spinbox->setValue(value);
   emit valueChanged(value);
}

float HFloatSlider::getValue() {
    float value = ui->spinbox->value();
    return value;
}

void HFloatSlider::onValueSliderChanged(int value)
{
    float newval = value;
    ui->spinbox->setValue(newval);
    emit valueChanged(newval);
    qDebug() << "slider change"  << newval;
}

void HFloatSlider::onValueSpinboxChanged(double value)
{

    int newval = value;
    ui->slider->setValue(newval);
    emit valueChanged(value);
    qDebug() << "spinbox change"  << newval;

}

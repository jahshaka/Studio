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

    precision = 1000;
    ui->slider->setRange(0,precision);

    this->setRange(0,100);
}

HFloatSlider::~HFloatSlider()
{
    delete ui;
}

/**
 * Sets the range for the slider and also keeps the slider's value within the said range
 * @param minVal
 * @param maxVal
 */
void HFloatSlider::setRange(float minVal,float maxVal)
{
    if(value<minVal)
        value = minVal;
    if(value>maxVal)
        value = maxVal;

    this->minVal = minVal;
    this->maxVal = maxVal;

    ui->spinbox->setRange(minVal,maxVal);
}

void  HFloatSlider::setValue( float value ){
    if(this->value!=value)
    {
        this->value = value;
        ui->spinbox->setValue(value);

        float mappedValue = (value-minVal)/(maxVal-minVal);
        ui->slider->setValue((int)(mappedValue*precision));

        emit valueChanged(value);
    }

}

float HFloatSlider::getValue() {
    return value;
}

void HFloatSlider::onValueSliderChanged(int value)
{
    float range = (float)value/precision;
    this->value = (maxVal-minVal)*range;

    ui->spinbox->setValue(value);
    emit valueChanged(value);
}

void HFloatSlider::onValueSpinboxChanged(double value)
{
    this->value = value;

    float mappedValue = (value-minVal)/(maxVal-minVal);
    ui->slider->setValue((int)(mappedValue*precision));

    emit valueChanged(value);
}

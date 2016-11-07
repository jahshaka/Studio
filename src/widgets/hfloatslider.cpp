#include "hfloatslider.h"
#include "ui_hfloatslider.h"

HFloatSlider::HFloatSlider(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::HFloatSlider)
{
    ui->setupUi(this);
    connect(ui->spinbox,SIGNAL(valueChanged(int)),ui->slider,SLOT(setValue(int)));
    connect(ui->slider,SIGNAL(valueChanged(int)),ui->spinbox,SLOT(setValue(int)));
}

HFloatSlider::~HFloatSlider()
{
    delete ui;
}

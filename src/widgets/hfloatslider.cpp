#include "hfloatslider.h"
#include "ui_hfloatslider.h"

HFloatSlider::HFloatSlider(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::HFloatSlider)
{
    ui->setupUi(this);
}

HFloatSlider::~HFloatSlider()
{
    delete ui;
}

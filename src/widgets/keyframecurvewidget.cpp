#include "keyframecurvewidget.h"
#include "ui_keyframecurvewidget.h"

KeyFrameCurveWidget::KeyFrameCurveWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::KeyFrameCurveWidget)
{
    ui->setupUi(this);
}

KeyFrameCurveWidget::~KeyFrameCurveWidget()
{
    delete ui;
}

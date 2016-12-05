#include "checkboxproperty.h"
#include "ui_checkboxproperty.h"
#include <QCheckBox>

CheckBoxProperty::CheckBoxProperty(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::checkboxproperty)
{
    ui->setupUi(this);
    connect(ui->checkBox,SIGNAL(valueChanged(bool)),SLOT(onCheckboxValueChanged(bool)));
    connect(ui->checkBox,SIGNAL(clicked(bool)),SLOT(onCheckboxValueChanged(bool)));
}

CheckBoxProperty::~CheckBoxProperty()
{
    delete ui;
}


void CheckBoxProperty::setValue(bool val)
{
    ui->checkBox->setChecked(val);
}

void CheckBoxProperty::setLabel(QString label)
{
    ui->label->setText(label);
}

void CheckBoxProperty::onCheckboxValueChanged(bool val)
{
    emit valueChanged(val);
}

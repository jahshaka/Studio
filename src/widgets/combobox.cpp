#include "combobox.h"
#include "ui_combobox.h"

ComboBox::ComboBox(QWidget* parent) : QWidget(parent), ui(new Ui::ComboBox)
{
    ui->setupUi(this);
}

ComboBox::~ComboBox()
{
    delete ui;
}

void ComboBox::setLabel(QString label)
{
    ui->label->setText(label);
}

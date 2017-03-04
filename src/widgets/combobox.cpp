#include "combobox.h"
#include "ui_combobox.h"

#include <QDebug>

ComboBox::ComboBox(QWidget* parent) : QWidget(parent), ui(new Ui::ComboBox)
{
    ui->setupUi(this);

    connect(ui->comboBox, SIGNAL(currentTextChanged(QString)), SLOT(onDropDownTextChanged(QString)));
}

ComboBox::~ComboBox()
{
    delete ui;
}

void ComboBox::setLabel(QString label)
{
    ui->label->setText(label);
}

void ComboBox::addItem(QString item)
{
    ui->comboBox->addItem(item);
}

void ComboBox::onDropDownTextChanged(QString text)
{

}

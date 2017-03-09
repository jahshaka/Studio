#include "comboboxwidget.h"
#include "ui_comboboxwidget.h"

#include <QDebug>

ComboBoxWidget::ComboBoxWidget(QWidget* parent) : QWidget(parent), ui(new Ui::ComboBoxWidget)
{
    ui->setupUi(this);

    connect(ui->comboBox, SIGNAL(currentTextChanged(QString)), SLOT(onDropDownTextChanged(QString)));
}

ComboBoxWidget::~ComboBoxWidget()
{
    delete ui;
}

void ComboBoxWidget::setLabel(const QString& label)
{
    ui->label->setText(label);
}

void ComboBoxWidget::addItem(const QString& item)
{
    ui->comboBox->addItem(item);
}

void ComboBoxWidget::onDropDownTextChanged(const QString& text)
{

}

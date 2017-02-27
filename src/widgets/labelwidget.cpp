#include "labelwidget.h"
#include "ui_labelwidget.h"

LabelWidget::LabelWidget(QWidget* parent) : QWidget(parent), ui(new Ui::LabelWidget)
{
    ui->setupUi(this);

    // for some reason the palette cannot be set and stylesheet changes
    // don't apply after the main app is run, set any styles here... !
    //ui->lineEdit->setStyleSheet("background-color: red; padding: 8px; margin: 0");
}

LabelWidget::~LabelWidget()
{

}

void LabelWidget::setLabel(const QString& label)
{
    ui->label->setText(label);
}

void LabelWidget::setText(const QString& text)
{
    ui->display->setText(text);
}

void LabelWidget::clearText()
{
    ui->display->clear();
}

void LabelWidget::onTextInputChanged(const QString& val)
{

}

#include "textinput.h"
#include "ui_textinput.h"

TextInput::TextInput(QWidget* parent) : QWidget(parent), ui(new Ui::TextInput)
{
    ui->setupUi(this);

    // for some reason the palette cannot be set and stylesheet changes
    // don't apply after the main app is run, set any styles here... !
    //ui->lineEdit->setStyleSheet("background-color: red; padding: 8px; margin: 0");
}

TextInput::~TextInput()
{

}

void TextInput::setLabel(const QString& label)
{
    ui->label->setText(label);
}

void TextInput::setText(const QString& text)
{
    ui->lineEdit->setText(text);
}

void TextInput::clearText()
{
    ui->lineEdit->clear();
}

QString TextInput::getText()
{
    return ui->lineEdit->text();
}

void TextInput::onTextInputChanged(const QString& val)
{

}

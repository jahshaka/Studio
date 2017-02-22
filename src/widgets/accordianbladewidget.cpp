/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "accordianbladewidget.h"
#include "ui_accordianbladewidget.h"
#include "hfloatslider.h"
#include "ui_hfloatslider.h"
#include "colorvaluewidget.h"
#include "texturepicker.h"
#include "ui_texturepicker.h"
#include "transformeditor.h"
#include "filepickerwidget.h"
#include "ui_filepickerwidget.h"
#include "checkboxproperty.h"
#include "combobox.h"
#include "textinput.h"
#include "ui_combobox.h"
#include <QSpinBox>
#include <QDebug>

// these will replace all
// @TODO MAKE THIS A TEMPLATE
HFloatSlider* AccordianBladeWidget::addValueSlider(QString label, float start, float end) {
    HFloatSlider *slider = new HFloatSlider;
    slider->ui->label->setText(label);
    slider->setRange(start, end);

    ui->contentpane->layout()->addWidget(slider);

    return slider;
}

AccordianBladeWidget::AccordianBladeWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AccordianBladeWidget)
{
    ui->setupUi(this);
    minimum_height = 11*4;//default margin is 11
    this->setMinimumHeight( ui->bg->height() );
    //QVBoxLayout *layout = new QVBoxLayout();

    //delete ui->contentpane->layout();
    //ui->contentpane->setLayout(layout);
    ui->contentpane->setVisible(false);
    ui->toggle->setStyleSheet("QPushButton { border-image: url(:/app/icons/right-chevron.svg); } QPushButton:hover { background-color: rgb(30, 144, 255); border: none;}");
}

AccordianBladeWidget::~AccordianBladeWidget()
{
    delete ui;
}

void AccordianBladeWidget::on_toggle_clicked()
{
    bool state = ui->contentpane->isVisible();

    if( state == true )
    {
        ui->toggle->setStyleSheet("QPushButton { border-image: url(:/app/icons/right-chevron.svg); } QPushButton:hover { background-color: rgb(30, 144, 255); border: none;}");
        ui->contentpane->setVisible(false);
        auto bgHeight = ui->bg->height();
        this->setMinimumHeight( bgHeight );
    }
    else
    {
        ui->toggle->setStyleSheet("QPushButton { border-image: url(:/app/icons/chevron-arrow-down.svg); } QPushButton:hover { background-color: rgb(30, 144, 255); border: none;}");
        ui->contentpane->setVisible(true);
        if( minimum_height ){

        }
        this->setMinimumHeight( minimum_height );
        this->setMaximumHeight( minimum_height );

        //this->setMinimumHeight( ui->contentpane->layout()->totalMinimumSize().height() + -ui->contentpane->layout()->contentsRect().height() );
        //this->setMaximumHeight( ui->contentpane->layout()->totalMinimumSize().height() + -ui->contentpane->layout()->contentsRect().height());
        //qDebug() << "MINIMUM HEIGHT CALC" << minimum_height ;
    }
}

void AccordianBladeWidget::setMaxHeight( int height){
    this->setMaximumHeight( height );
}

void AccordianBladeWidget::setContentTitle( QString title ){

    ui->content_title->setText(title);

}

TransformEditor* AccordianBladeWidget::addTransform(){
    TransformEditor *transform = new TransformEditor();
    int height = transform->height();
    minimum_height = height + 50;

    ui->contentpane->layout()->addWidget(transform);
    ui->contentpane->layout()->setMargin(0);
    return transform;
}

ColorValueWidget* AccordianBladeWidget::addColorPicker( QString name )
{

    ColorValueWidget *colorpicker = new ColorValueWidget();
    int height = colorpicker->height();
    minimum_height += height;
    minimum_height += 10;

    colorpicker->setLabel(name);
    ui->contentpane->layout()->addWidget(colorpicker);

    return colorpicker;
}

void AccordianBladeWidget::addFilePicker( QString name , QString fileextention ){
    FilePickerWidget *filepicker = new FilePickerWidget();
    int height = filepicker->height();
    filepicker->file_extentions = fileextention;
    minimum_height += height;
    minimum_height += 10;

    filepicker->ui->label->setText(name);
    ui->contentpane->layout()->addWidget(filepicker);
}

TexturePicker* AccordianBladeWidget::addTexturePicker( QString name ){

    TexturePicker *texpicker = new TexturePicker();
    int height = texpicker->height();
    minimum_height += height;
    minimum_height += 10;

    texpicker->ui->label->setText(name);
    ui->contentpane->layout()->addWidget(texpicker);
    return texpicker;
}

HFloatSlider* AccordianBladeWidget::addFloatValueSlider( QString name, float range_1 , float range_2 )
{
    HFloatSlider *slider = new HFloatSlider();
    int height = slider->height();
    minimum_height += height;
    minimum_height += 10;

    //minimum_height = ui->contentpane->layout()->totalMinimumSize().height();

    slider->ui->label->setText(name);
    slider->setRange(range_1,range_2);

    ui->contentpane->layout()->addWidget(slider);

    return slider;
}

CheckBoxProperty* AccordianBladeWidget::addCheckBox( QString name, bool value )
{
    auto checkbox = new CheckBoxProperty();
    checkbox->setLabel(name);
    ui->contentpane->layout()->addWidget(checkbox);
    int height = checkbox->height();

    minimum_height += height;
    minimum_height += 10;

    return checkbox;
}

ComboBox* AccordianBladeWidget::addComboBox(QString name)
{
    ComboBox* combobox = new ComboBox();
    combobox->setLabel(name);
    ui->contentpane->layout()->addWidget(combobox);
    int height = combobox->height();

    minimum_height += height;
    minimum_height += 10;

    return combobox;
}

TextInput* AccordianBladeWidget::addTextInput(QString name)
{
    TextInput* textInput = new TextInput();
    textInput->setLabel(name);
    ui->contentpane->layout()->addWidget(textInput);
    int height = textInput->height();

    minimum_height += height;
    minimum_height += 10;

    return textInput;
}

void AccordianBladeWidget::expand()
{
    ui->contentpane->setVisible(true);
    this->setMinimumHeight(minimum_height);
    this->setMaximumHeight(minimum_height);
}

void AccordianBladeWidget::expand2()
{
    ui->contentpane->setVisible(true);
    this->setMinimumHeight(0);
    this->setMaximumHeight(40);
}


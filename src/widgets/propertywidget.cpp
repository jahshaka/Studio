#include "propertywidget.h"
#include "ui_propertywidget.h"

#include "hfloatsliderwidget.h"
#include "ui_hfloatsliderwidget.h"

#include "colorvaluewidget.h"
#include "ui_colorvaluewidget.h"

#include "checkboxwidget.h"
#include "ui_checkboxwidget.h"

#include "texturepickerwidget.h"
#include "ui_texturepickerwidget.h"

#include "filepickerwidget.h"
#include "ui_filepickerwidget.h"

#include <QDebug>

PropertyWidget::PropertyWidget(QWidget *parent) : QWidget(parent), ui(new Ui::PropertyWidget)
{
    ui->setupUi(this);
    minimum_height = stretch = 0;
    ui->contentpane->setStyleSheet("background: green");
}

PropertyWidget::~PropertyWidget()
{
    delete ui;
}

HFloatSliderWidget* PropertyWidget::addFloatValueSlider(const QString& name, float start, float end)
{
    auto slider = new HFloatSliderWidget();
    slider->ui->label->setText(name);
    slider->setRange(start, end);

    minimum_height += slider->height() + stretch;

//    ui->contentpane->layout()->addWidget(slider);
    return slider;
}

ColorValueWidget* PropertyWidget::addColorPicker(const QString& name)
{
    auto colorpicker = new ColorValueWidget();
    colorpicker->setLabel(name);

    minimum_height += colorpicker->height() + stretch;

//    ui->contentpane->layout()->addWidget(colorpicker);
    return colorpicker;
}

CheckBoxWidget* PropertyWidget::addCheckBox(const QString& title)
{
    auto checkbox = new CheckBoxWidget();
    checkbox->setLabel(title);

    minimum_height += checkbox->height() + stretch;

//    ui->contentpane->layout()->addWidget(checkbox);
    return checkbox;
}

TexturePickerWidget* PropertyWidget::addTexturePicker(const QString& name)
{
    auto texpicker = new TexturePickerWidget();
    texpicker->ui->label->setText(name);

    minimum_height += texpicker->height() + stretch;

//    ui->contentpane->layout()->addWidget(texpicker);
    return texpicker;
}

FilePickerWidget* PropertyWidget::addFilePicker(const QString &name)
{
    FilePickerWidget *filePicker = new FilePickerWidget();
    filePicker->ui->label->setText(name);
//    filePicker->suffix = suffix;

    minimum_height += filePicker->height() + stretch;

//    ui->contentpane->layout()->addWidget(filePicker);
    return filePicker;
}

void PropertyWidget::setProperties(QList<Property*> properties)
{
    for (auto prop : properties) {
        switch (prop->type) {
            case PropertyType::Float:
                ui->contentpane->layout()->addWidget(addFloatValueSlider(prop->displayName, 10, 20));
            break;
            case PropertyType::Color:
                ui->contentpane->layout()->addWidget(addColorPicker(prop->displayName));
            break;
            case PropertyType::Bool:
                ui->contentpane->layout()->addWidget(addCheckBox(prop->displayName));
            break;
            case PropertyType::Texture:
                ui->contentpane->layout()->addWidget(addTexturePicker(prop->displayName));
            break;
            case PropertyType::File:
                ui->contentpane->layout()->addWidget(addFilePicker(prop->displayName));
            break;
            default: break;
        }
    }

    num_props = properties.size();
}

void PropertyWidget::update()
{

}

int PropertyWidget::getHeight()
{
    return minimum_height + (num_props * 6) /* spacing */;
}

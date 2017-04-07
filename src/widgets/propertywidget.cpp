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

void PropertyWidget::addFloatProperty(Property *prop)
{
    auto floatProp = static_cast<FloatProperty*>(prop);
    ui->contentpane->layout()->addWidget(addFloatValueSlider(floatProp->displayName,
                                                             floatProp->minValue,
                                                             floatProp->maxValue));
    properties.append(prop);
}

void PropertyWidget::addIntProperty(Property *prop)
{
    auto intProp = static_cast<IntProperty*>(prop);
    ui->contentpane->layout()->addWidget(addFloatValueSlider(intProp->displayName,
                                                             intProp->minValue,
                                                             intProp->maxValue));
    properties.append(prop);
}

void PropertyWidget::addColorProperty(Property *prop)
{
    auto colorProp = static_cast<ColorProperty*>(prop);
    ui->contentpane->layout()->addWidget(addColorPicker(colorProp->displayName));
    properties.append(prop);
}

void PropertyWidget::addBoolProperty(Property *prop)
{
    auto boolProp = static_cast<BoolProperty*>(prop);
    ui->contentpane->layout()->addWidget(addCheckBox(boolProp->displayName));
    properties.append(prop);
}

void PropertyWidget::addTextureProperty(Property *prop)
{
    auto textureProp = static_cast<TextureProperty*>(prop);
    ui->contentpane->layout()->addWidget(addTexturePicker(textureProp->displayName));
    properties.append(prop);
}

void PropertyWidget::addFileProperty(Property *prop)
{
    auto fileProp = static_cast<FileProperty*>(prop);
    ui->contentpane->layout()->addWidget(addFilePicker(fileProp->displayName));
    properties.append(prop);
}

void PropertyWidget::setProperties(QList<Property*> properties)
{
    for (auto prop : properties) {
        switch (prop->type) {
        case PropertyType::Float:
            addFloatProperty(prop);
        break;

        case PropertyType::Int:
            addIntProperty(prop);
        break;

        case PropertyType::Color:
            addColorProperty(prop);
        break;

        case PropertyType::Bool:
            addBoolProperty(prop);
        break;

        case PropertyType::Texture:
            addTextureProperty(prop);
        break;

        case PropertyType::File:
            addFileProperty(prop);
        break;

        case PropertyType::List: {
            break;
        }

        case PropertyType::Vec2: {
            break;
        }

        case PropertyType::Vec3: {
            break;
        }

        case PropertyType::None:
        default: break;
        }
    }

    this->properties = properties;
}

void PropertyWidget::update()
{

}

int PropertyWidget::getHeight()
{
    return minimum_height + (properties.size() * 6) /* magic spacing */;
}

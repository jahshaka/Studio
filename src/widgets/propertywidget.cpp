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
    progressiveHeight = stretch = 0;
}

PropertyWidget::~PropertyWidget()
{
    delete ui;
}

HFloatSliderWidget* PropertyWidget::addFloatValueSlider(const QString& name, float min, float max)
{
    auto slider = new HFloatSliderWidget();
    slider->ui->label->setText(name);
    slider->setRange(min, max);
    progressiveHeight += slider->height() + stretch;

    return slider;
}

ColorValueWidget* PropertyWidget::addColorPicker(const QString& name)
{
    auto colorpicker = new ColorValueWidget();
    colorpicker->setLabel(name);
    progressiveHeight += colorpicker->height() + stretch;

    return colorpicker;
}

CheckBoxWidget* PropertyWidget::addCheckBox(const QString& title)
{
    auto checkbox = new CheckBoxWidget();
    checkbox->setLabel(title);
    progressiveHeight += checkbox->height() + stretch;

    return checkbox;
}

TexturePickerWidget* PropertyWidget::addTexturePicker(const QString& name)
{
    auto texpicker = new TexturePickerWidget();
    texpicker->ui->label->setText(name);
    progressiveHeight += texpicker->height() + stretch;

    return texpicker;
}

FilePickerWidget* PropertyWidget::addFilePicker(const QString &name, const QString &suffix)
{
    FilePickerWidget *filePicker = new FilePickerWidget();
    filePicker->ui->label->setText(name);
    filePicker->suffix = suffix;
    progressiveHeight += filePicker->height() + stretch;

    return filePicker;
}

void PropertyWidget::addFloatProperty(Property *prop)
{
    auto fltProp = static_cast<FloatProperty*>(prop);
    auto fltWidget = addFloatValueSlider(fltProp->displayName, fltProp->minValue, fltProp->maxValue);

    fltWidget->index = prop->id;
    fltWidget->setValue(fltProp->getValue().toFloat());
    ui->contentpane->layout()->addWidget(fltWidget);
    properties.append(prop);

    connect(fltWidget, HFloatSliderWidget::valueChanged, this, [this, fltProp](float value) {
        fltProp->value = value;

        if (listener) {
            listener->onPropertyChanged(fltProp);
        }

        emit onPropertyChanged(fltProp);
    });
}

void PropertyWidget::addIntProperty(Property *prop)
{
    auto intProp = static_cast<IntProperty*>(prop);
    auto intWidget = addFloatValueSlider(intProp->displayName, intProp->minValue, intProp->maxValue);

    intWidget->index = prop->id;
    ui->contentpane->layout()->addWidget(intWidget);
    properties.append(prop);
}

void PropertyWidget::addColorProperty(Property *prop)
{
    auto colorProp = static_cast<ColorProperty*>(prop);
    auto colorWidget = addColorPicker(colorProp->displayName);

    colorWidget->index = prop->id;
    colorWidget->setColorValue(colorProp->getValue().value<QColor>());
    ui->contentpane->layout()->addWidget(colorWidget);
    properties.append(prop);

    connect(colorWidget->getPicker(), ColorPickerWidget::onColorChanged, this,
           [this, colorProp](QColor value)
    {
        colorProp->value = value;

        if (listener) {
            listener->onPropertyChanged(colorProp);
        }

        emit onPropertyChanged(colorProp);
    });
}

void PropertyWidget::addBoolProperty(Property *prop)
{
    auto boolProp = static_cast<BoolProperty*>(prop);
    auto boolWidget = addCheckBox(boolProp->displayName);

    boolWidget->index = prop->id;
    boolWidget->setValue(boolProp->getValue().toBool());
    ui->contentpane->layout()->addWidget(boolWidget);
    properties.append(prop);

    connect(boolWidget, CheckBoxWidget::valueChanged, this, [this, boolProp](bool value) {
        boolProp->value = value;

        if (listener) {
            listener->onPropertyChanged(boolProp);
        }

        emit onPropertyChanged(boolProp);
    });
}

void PropertyWidget::addTextureProperty(Property *prop)
{
    auto textureProp = static_cast<TextureProperty*>(prop);
    auto textureWidget = addTexturePicker(textureProp->displayName);

    textureWidget->index = prop->id;
    textureWidget->setTexture(textureProp->getValue().toString());
    ui->contentpane->layout()->addWidget(textureWidget);
    properties.append(prop);

    connect(textureWidget, TexturePickerWidget::valueChanged, this,
           [this, textureProp](QString value)
    {
        textureProp->value = value;

        if (listener) {
            listener->onPropertyChanged(textureProp);
        }

        emit onPropertyChanged(textureProp);
    });
}

void PropertyWidget::addFileProperty(Property *prop)
{
    auto fileProp = static_cast<FileProperty*>(prop);
    auto fileWidget = addFilePicker(fileProp->displayName, fileProp->suffix);

    fileWidget->index = prop->id;
    fileWidget->setFilepath(fileProp->getValue().toString());
    ui->contentpane->layout()->addWidget(fileWidget);
    properties.append(prop);

    connect(fileWidget, FilePickerWidget::onPathChanged, this, [this, fileProp](QString value) {
        fileProp->value = value;

        if (listener) {
            listener->onPropertyChanged(fileProp);
        }

        emit onPropertyChanged(fileProp);
    });
}

void PropertyWidget::setListener(PropertyListener *listener)
{
    this->listener = listener;
}

void PropertyWidget::updatePane()
{

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

            case PropertyType::List:
            break;

            case PropertyType::Vec2:
            break;

            case PropertyType::Vec3:
            break;

            case PropertyType::None:
            default: break;
        }
    }

    updatePane();

    this->properties = properties;
}

int PropertyWidget::getHeight()
{
    return progressiveHeight + (properties.size() * ui->contentpane->layout()->spacing());
}

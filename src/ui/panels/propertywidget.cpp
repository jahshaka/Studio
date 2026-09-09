/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/qtinterop.h"
#include "irisgl/core/math/vec.h"
#include "ui/panels/propertywidget.h"
#include "ui_propertywidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui_hfloatsliderwidget.h"
#include "ui/controls/colorvaluewidget.h"
#include "ui_colorvaluewidget.h"
#include "ui/controls/checkboxwidget.h"
#include "ui_checkboxwidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui_texturepickerwidget.h"
#include "ui/controls/filepickerwidget.h"
#include "ui_filepickerwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include <QDir>
#include "data/database/database.h"
#include "ui/controls/rowfit.h"

PropertyWidget::PropertyWidget(QWidget *parent) : QWidget(parent), ui(new Ui::PropertyWidget)
{
    ui->setupUi(this);
    progressiveHeight = stretch = 0;
}

PropertyWidget::~PropertyWidget()
{
    delete ui;
}

/// The material list's rows enter the panel HERE, so they get the same
/// dock-fitting the accordion's rows get (ui/controls/rowfit.h): a texture or
/// preset name elides, it does not push the Properties dock wider than the
/// column (owner report 2026-09-08).
void PropertyWidget::addRow(QWidget *row)
{
    if (!row) return;
    RowFit::fitRow(row);
    ui->contentpane->layout()->addWidget(row);
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
    texpicker->project = project;
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

void PropertyWidget::addFloatProperty(iris::Property *prop)
{
    auto fltProp = static_cast<iris::FloatProperty*>(prop);
    auto fltWidget = addFloatValueSlider(fltProp->displayName, fltProp->minValue, fltProp->maxValue);

    fltWidget->index = prop->id;
    fltWidget->setValue(fltProp->getValue().toFloat());
    addRow(fltWidget);
    properties.append(prop);
    rowByName.insert(prop->name, fltWidget);

    connect(fltWidget, &HFloatSliderWidget::valueChanged, this, [this, fltProp](float value) {
        fltProp->value = value;

        if (listener) {
            listener->onPropertyChanged(fltProp);
        }

        emit onPropertyChanged(fltProp);
    });

    connect(fltWidget, &HFloatSliderWidget::valueChangeStart, this, [this, fltProp](float value) {
        fltProp->value = value;

        if (listener) {
            listener->onPropertyChangeStart(fltProp);
        }

        emit onPropertyChanged(fltProp);
    });

    connect(fltWidget, &HFloatSliderWidget::valueChangeEnd, this, [this, fltProp](float value) {
        fltProp->value = value;

        if (listener) {
            listener->onPropertyChangeEnd(fltProp);
        }

        emit onPropertyChanged(fltProp);
    });
}

// The generic ENUM row. An enum wearing a slider reads as a meaningless 0..5,
// so a ListProperty renders as a labeled dropdown: combo index == the stored
// value, and the labels come off the ROW, not off a name branch here.
//
// That last part is the whole point. Until HLMS_ADOPTION P1 this was a
// hardcoded `if (name == "alphaMode")` and PropertyType::List rendered
// NOTHING, so every new enum meant another special case. The failure that
// justifies the labels living on the row: a material set to a mode the panel
// had no label for showed a BLANK combo, and any pick then silently
// downgraded it — Refractive was missing exactly that way (PUBLISH_AUDIT #4).
// With the vocabulary on the property, the picker and the document cannot
// disagree.
void PropertyWidget::addEnumProperty(iris::Property *prop)
{
    auto listProp = static_cast<iris::ListProperty*>(prop);

    auto combo = new ComboBoxWidget();
    combo->setLabel(listProp->displayName);
    for (const QString &label : listProp->labels) combo->addItem(label);
    combo->index = prop->id;
    combo->setCurrentIndex(listProp->getValue().toInt());
    progressiveHeight += combo->height() + stretch;
    addRow(combo);
    properties.append(prop);
    rowByName.insert(prop->name, combo);

    connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, [this, listProp](int idx) {
        if (listProp->getValue().toInt() == idx) return;
        // Start must see the OLD value (it records the undo baseline),
        // End the new one — a combo pick is a complete one-shot gesture.
        if (listener) listener->onPropertyChangeStart(listProp);
        listProp->value = idx;
        if (listener) {
            listener->onPropertyChanged(listProp);
            listener->onPropertyChangeEnd(listProp);
        }
        emit onPropertyChanged(listProp);
        // A pick can change which OTHER rows are available (BRDF -> clear coat).
        applyRowConstraints();
    });
}

void PropertyWidget::addIntProperty(iris::Property *prop)
{
    auto intProp = static_cast<iris::IntProperty*>(prop);

    auto intWidget = addFloatValueSlider(intProp->displayName, intProp->minValue, intProp->maxValue);

    intWidget->index = prop->id;
    intWidget->setValue(float(intProp->getValue().toInt()));
    addRow(intWidget);
    properties.append(prop);
    rowByName.insert(prop->name, intWidget);

    // Same wiring as the float rows (this row had none at all - the panel's int
    // properties, e.g. a material's Alpha Mode, silently did nothing).
    connect(intWidget, &HFloatSliderWidget::valueChanged, this, [this, intProp](float value) {
        intProp->value = qRound(value);

        if (listener) {
            listener->onPropertyChanged(intProp);
        }

        emit onPropertyChanged(intProp);
    });

    connect(intWidget, &HFloatSliderWidget::valueChangeStart, this, [this, intProp](float value) {
        intProp->value = qRound(value);

        if (listener) {
            listener->onPropertyChangeStart(intProp);
        }

        emit onPropertyChanged(intProp);
    });

    connect(intWidget, &HFloatSliderWidget::valueChangeEnd, this, [this, intProp](float value) {
        intProp->value = qRound(value);

        if (listener) {
            listener->onPropertyChangeEnd(intProp);
        }

        emit onPropertyChanged(intProp);
    });
}

void PropertyWidget::addColorProperty(iris::Property *prop)
{
    auto colorProp = static_cast<iris::ColorProperty*>(prop);
    auto colorWidget = addColorPicker(colorProp->displayName);

    colorWidget->index = prop->id;
    colorWidget->setColorValue(colorProp->getValue().value<QColor>());
    addRow(colorWidget);
    properties.append(prop);
    rowByName.insert(prop->name, colorWidget);

    connect(colorWidget->getPicker(), &ColorPickerWidget::onColorChanged, this,
           [this, colorProp](QColor value)
    {
        colorProp->value = value;

        if (listener) {
            listener->onPropertyChanged(colorProp);
        }

        emit onPropertyChanged(colorProp);
    });

    // The popup session brackets the live changes above into one undo entry
    // (start fires before any change, so the listener records the old colour).
    connect(colorWidget->getPicker(), &ColorPickerWidget::pickingStarted, this, [this, colorProp]() {
        if (listener) listener->onPropertyChangeStart(colorProp);
    });
    connect(colorWidget->getPicker(), &ColorPickerWidget::pickingEnded, this, [this, colorProp]() {
        if (listener) listener->onPropertyChangeEnd(colorProp);
    });
}

void PropertyWidget::addBoolProperty(iris::Property *prop)
{
    auto boolProp = static_cast<iris::BoolProperty*>(prop);
    auto boolWidget = addCheckBox(boolProp->displayName);

    boolWidget->index = prop->id;
    boolWidget->setValue(boolProp->getValue().toBool());
    addRow(boolWidget);
    properties.append(prop);
    rowByName.insert(prop->name, boolWidget);

    connect(boolWidget, &CheckBoxWidget::valueChanged, this, [this, boolProp](bool value) {
        // A checkbox toggle is one discrete gesture - one undo entry.
        if (listener) listener->onPropertyChangeStart(boolProp);

        boolProp->value = value;

        if (listener) {
            listener->onPropertyChanged(boolProp);
            listener->onPropertyChangeEnd(boolProp);
        }

        emit onPropertyChanged(boolProp);
    });
}

void PropertyWidget::addTextureProperty(iris::Property *prop)
{
    auto textureProp = static_cast<iris::TextureProperty*>(prop);
    auto textureWidget = addTexturePicker(textureProp->displayName);

    textureWidget->index = prop->id;

	auto texturePath = prop->getValue().toString();

    textureWidget->setTexture(texturePath);
    addRow(textureWidget);
    properties.append(prop);
    rowByName.insert(prop->name, textureWidget);

    connect(textureWidget, &TexturePickerWidget::valueChanged, this,
           [this, textureProp](QString value)
    {
        // Picking a texture is a single discrete gesture: bracket it with
        // change start/end so it lands as one undo entry. Start must run
        // BEFORE the write - the listener records the old value from the prop.
        if (listener) listener->onPropertyChangeStart(textureProp);

        textureProp->value = value;

        if (listener) {
            listener->onPropertyChanged(textureProp);
            listener->onPropertyChangeEnd(textureProp);
        }

        emit onPropertyChanged(textureProp);
    });
}

void PropertyWidget::addFileProperty(iris::Property *prop)
{
    auto fileProp = static_cast<iris::FileProperty*>(prop);
    auto fileWidget = addFilePicker(fileProp->displayName, fileProp->suffix);

    fileWidget->index = prop->id;
    fileWidget->setFilepath(fileProp->getValue().toString());
    addRow(fileWidget);
    properties.append(prop);

    connect(fileWidget, &FilePickerWidget::onPathChanged, this, [this, fileProp](QString value) {
        fileProp->value = value;

        if (listener) {
            listener->onPropertyChanged(fileProp);
        }

        emit onPropertyChanged(fileProp);
    });
}

void PropertyWidget::addVector2Property(iris::Property *prop)
{
	auto vecProp = static_cast<iris::Vec2Property*>(prop);
	auto widget = addVector2Widget(vecProp->displayName, vecProp->value.x(), vecProp->value.y());
	auto holder = addWidgetHolder(vecProp->displayName, widget);
	addRow(holder);
	properties.append(vecProp);

	connect(widget, &Widget2D::valueChanged, [=](iris::Vec2 value) {
		vecProp->value = iris::toQt(value);
		if (listener) listener->onPropertyChanged(vecProp);
		emit onPropertyChanged(vecProp);
	});

}

void PropertyWidget::addVector3Property(iris::Property *prop)
{
	auto vecProp = static_cast<iris::Vec3Property*>(prop);
	auto widget = addVector3Widget(vecProp->displayName, vecProp->value.x(), vecProp->value.y(), vecProp->value.z());
	auto holder = addWidgetHolder(vecProp->displayName, widget);
	addRow(holder);
	properties.append(vecProp);

	connect(widget, &Widget3D::valueChanged, [=](iris::Vec3 value) {
		vecProp->value = iris::toQt(value);
		if (listener) listener->onPropertyChanged(vecProp);
		emit onPropertyChanged(vecProp);
	});

}

void PropertyWidget::addVector4Property(iris::Property *prop)
{
	auto vecProp = static_cast<iris::Vec4Property*>(prop);
	auto widget = addVector4Widget(vecProp->displayName, vecProp->value.x(), vecProp->value.y(), vecProp->value.z(), vecProp->value.w());
	auto holder = addWidgetHolder(vecProp->displayName, widget);
	addRow(holder);
	properties.append(vecProp);

	connect(widget, &Widget4D::valueChanged, [=](iris::Vec4 value) {
		vecProp->value = iris::toQt(value);
		if (listener) listener->onPropertyChanged(vecProp);
		emit onPropertyChanged(vecProp);
	});

}

Widget2D * PropertyWidget::addVector2Widget(const QString &, float xValue, float yValue)
{
	auto widget = new Widget2D;
	widget->setValues(xValue, yValue);
	addRow(widget);
	progressiveHeight += widget->height() + stretch;

	return widget;
}

Widget3D * PropertyWidget::addVector3Widget(const QString &, float xValue, float yValue, float zValue)
{
	auto widget = new Widget3D;
	widget->setValues(xValue, yValue, zValue);
	addRow(widget);
	progressiveHeight += widget->height() + stretch;

	return widget;
}

Widget4D * PropertyWidget::addVector4Widget(const QString &, float xValue, float yValue, float zValue, float wValue)
{
	auto widget = new Widget4D;
	widget->setValues(xValue, yValue, zValue, wValue);
	addRow(widget);
	progressiveHeight += widget->height() + stretch;

	return widget;
}

QWidget * PropertyWidget::addWidgetHolder(QString title, QWidget* widget)
{

	auto holder = new QWidget;
	auto layout = new QHBoxLayout;
	holder->setLayout(layout);

	layout->addWidget(new QLabel(title, holder));
	layout->addStretch();
	layout->addWidget(widget);
	layout->setContentsMargins(0, 0, 0, 0);

	holder->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
	widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

	return holder;
}


void PropertyWidget::setListener(iris::PropertyListener *listener)
{
    this->listener = listener;
}

void PropertyWidget::updatePane()
{

}

void PropertyWidget::setProperties(QList<iris::Property*> properties)
{
    rowByName.clear();
    for (auto prop : properties)
        switch (prop->type) {
            case iris::PropertyType::Float:
                addFloatProperty(prop);
            break;

            case iris::PropertyType::Int:
                addIntProperty(prop);
            break;

            case iris::PropertyType::Color:
                addColorProperty(prop);
            break;

            case iris::PropertyType::Bool:
                addBoolProperty(prop);
            break;

            case iris::PropertyType::Texture:
                addTextureProperty(prop);
            break;

            case iris::PropertyType::File:
                addFileProperty(prop);
            break;

            case iris::PropertyType::List:
                addEnumProperty(prop);
            break;

            case iris::PropertyType::Vec2:
				addVector2Property(prop);
            break;

			case iris::PropertyType::Vec3:
				addVector3Property(prop);
			break;
			case iris::PropertyType::Vec4:
				addVector4Property(prop);
			break;

            case iris::PropertyType::None:
            default: break;
        }

    updatePane();

    this->properties = properties;
    applyRowConstraints();
}

void PropertyWidget::applyRowConstraints()
{
    // A row that silently does nothing is the defect class this whole program
    // exists to remove, so the two places the renderer imposes a constraint
    // both show up here as a GREYED ROW WITH A REASON. Nothing is ever cleared:
    // the authored values stay in the document and come back the moment the
    // constraint lifts, so an experiment is always reversible.
    auto valueOf = [this](const QString &name, bool *found) {
        for (auto *prop : properties)
            if (prop && prop->name == name) { if (found) *found = true; return prop->getValue().toInt(); }
        if (found) *found = false;
        return 0;
    };
    auto constrain = [this](const QString &name, bool enabled, const QString &why) {
        QWidget *w = rowByName.value(name);
        if (!w) return;
        w->setEnabled(enabled);
        w->setToolTip(enabled ? QString() : why);
    };

    // ---- 1. the SHADING MODEL (HLMS_ADOPTION P4a) ----
    // Unlit is a different renderer family, not a switch on this one: it has no
    // lighting term at all, so metalness, roughness, normals, emissive, the
    // BRDF, the clear coat, shadow reception and their maps have nothing to
    // reach. Texture tiling is in the list for a narrower reason — uvScale
    // rides a shader piece belonging to the lit family, and the unlit
    // equivalent is deliberately out of v1 (decision D-P4a).
    bool haveShading = false;
    const int shadingModel = valueOf(QStringLiteral("shadingModel"), &haveShading);
    const bool unlit = haveShading && shadingModel == 1;
    // DISTORTION (POST_LOOKS_SPEC.md §5.2) greys far more: it draws no surface
    // at all, so only the displacement map, the strength (opacity) and
    // two-sidedness mean anything. Handled as its own branch rather than a
    // longer list on the same one, because the REASON shown to the user is a
    // different sentence and that is most of the value of greying a row.
    const bool distortion = haveShading && shadingModel == 2;
    if (haveShading) {
        // EVERY ROW EITHER MODEL CAN CONSTRAIN IS WRITTEN EXPLICITLY, and that
        // is the whole reason this is a union rather than one list per branch.
        // The two models constrain overlapping but DIFFERENT sets — Distortion
        // greys the base colour, which Unlit renders; Unlit greys the normal
        // map, which Distortion READS as its displacement field — so a row
        // touched by one model and not the other has to be told to come back.
        // Disabling only what the current model forbids leaves the previous
        // model's greying behind: Lit -> Unlit -> Distortion left the Normal
        // Map dead, i.e. the one row Distortion is authored through
        // (caught by ui.material_panel, 2026-09-09).
        const QVector<QString> &unlitRows = iris::PbrMaterial::rowsUnusedWhenUnlit();
        const QVector<QString> &distortRows = iris::PbrMaterial::rowsUnusedWhenDistortion();
        const QVector<QString> &forbidden = distortion ? distortRows : unlitRows;
        const QString why = distortion
            ? tr("Not used by the Distortion shading model — the object draws nothing of "
                 "itself, it warps what is behind it. Use the Normal Map as the displacement "
                 "and Opacity as the strength. The value is kept and returns when the model "
                 "does.")
            : tr("Not used by the Unlit shading model — it has no lighting. "
                 "The value is kept and returns when the model does.");
        QStringList touched;
        for (const QString &name : unlitRows) touched << name;
        for (const QString &name : distortRows)
            if (!touched.contains(name)) touched << name;
        const bool constrainedModel = unlit || distortion;
        for (const QString &name : touched)
            constrain(name, !(constrainedModel && forbidden.contains(name)), why);
    }

    // ---- 2. the clear coat's BRDF family (HLMS_ADOPTION P1) ----
    // Clear coat is only representable on the renderer's Default BRDF family
    // (the diffuse-fresnel variants of Default included).
    bool haveBrdf = false;
    const int brdfIndex = valueOf(QStringLiteral("brdf"), &haveBrdf);
    if (haveBrdf && !unlit && !distortion) {
        const bool coatOk = iris::PbrMaterial::brdfSupportsClearCoat(brdfIndex);
        const QString why = tr("Clear coat is only available on the Default BRDF family.");
        constrain(QStringLiteral("clearCoat"), coatOk, why);
        constrain(QStringLiteral("clearCoatRoughness"), coatOk, why);
    }
}

int PropertyWidget::getHeight()
{
    return progressiveHeight + (properties.size() * ui->contentpane->layout()->spacing());
}

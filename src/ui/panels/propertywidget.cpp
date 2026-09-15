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
#include "ui/panels/propertyrows.h"

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
    // THE SECOND CHOKE POINT (PROPERTY_FILTER_SPEC §3.2). This widget is itself
    // a row in the material blade, so it registers as a CONTAINER: its property
    // rows chain to it, and their key is the property's own name — "roughness"
    // finds the roughness row whatever the material calls it on screen.
    PropertyRows::registry().add(this, row);
    if (!pendingKey.isEmpty()) PropertyRows::registry().identify(row, pendingKey);
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
    // THE ROW BINDS TO A SLOT, NOT TO A POINTER (ADD-1, 2026-09-15). Every
    // handler below used to capture the iris::Property* it was built from,
    // which is what made a row single-use: showing another material's rows
    // meant destroying these and building new ones (44 ms of a mesh pick). A
    // slot is the row's INDEX in this panel's property list, so rebind() can
    // point the same rows at another material's properties and every handler
    // follows. It is also the end of a real hazard: a captured Property* whose
    // material was replaced was a dangling write waiting for a drag.
    const int slot = takeSlot(prop, fltWidget);
    rowByName.insert(prop->name, fltWidget);

    connect(fltWidget, &HFloatSliderWidget::valueChanged, this, [this, slot](float value) {
        auto *p = propertyAt(slot);
        if (!p) return;
        p->setValue(QVariant(value));

        if (listener) {
            listener->onPropertyChanged(p);
        }

        emit onPropertyChanged(p);
    });

    connect(fltWidget, &HFloatSliderWidget::valueChangeStart, this, [this, slot](float value) {
        auto *p = propertyAt(slot);
        if (!p) return;
        p->setValue(QVariant(value));

        if (listener) {
            listener->onPropertyChangeStart(p);
        }

        emit onPropertyChanged(p);
    });

    connect(fltWidget, &HFloatSliderWidget::valueChangeEnd, this, [this, slot](float value) {
        auto *p = propertyAt(slot);
        if (!p) return;
        p->setValue(QVariant(value));

        if (listener) {
            listener->onPropertyChangeEnd(p);
        }

        emit onPropertyChanged(p);
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
    const int slot = takeSlot(prop, combo);   // see addFloatProperty
    rowByName.insert(prop->name, combo);

    connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, [this, slot](int idx) {
        auto *listProp = static_cast<iris::ListProperty *>(propertyAt(slot));
        if (!listProp) return;
        if (listProp->getValue().toInt() == idx) return;
        // Start must see the OLD value (it records the undo baseline),
        // End the new one — a combo pick is a complete one-shot gesture.
        if (listener) listener->onPropertyChangeStart(listProp);
        listProp->setValue(QVariant(idx));
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
    const int slot = takeSlot(prop, intWidget);   // see addFloatProperty
    rowByName.insert(prop->name, intWidget);

    // Same wiring as the float rows (this row had none at all - the panel's int
    // properties, e.g. a material's Alpha Mode, silently did nothing).
    connect(intWidget, &HFloatSliderWidget::valueChanged, this, [this, slot](float value) {
        auto *p = propertyAt(slot);
        if (!p) return;
        p->setValue(QVariant(qRound(value)));

        if (listener) {
            listener->onPropertyChanged(p);
        }

        emit onPropertyChanged(p);
    });

    connect(intWidget, &HFloatSliderWidget::valueChangeStart, this, [this, slot](float value) {
        auto *p = propertyAt(slot);
        if (!p) return;
        p->setValue(QVariant(qRound(value)));

        if (listener) {
            listener->onPropertyChangeStart(p);
        }

        emit onPropertyChanged(p);
    });

    connect(intWidget, &HFloatSliderWidget::valueChangeEnd, this, [this, slot](float value) {
        auto *p = propertyAt(slot);
        if (!p) return;
        p->setValue(QVariant(qRound(value)));

        if (listener) {
            listener->onPropertyChangeEnd(p);
        }

        emit onPropertyChanged(p);
    });
}

void PropertyWidget::addColorProperty(iris::Property *prop)
{
    auto colorProp = static_cast<iris::ColorProperty*>(prop);
    auto colorWidget = addColorPicker(colorProp->displayName);

    colorWidget->index = prop->id;
    colorWidget->setColorValue(colorProp->getValue().value<QColor>());
    addRow(colorWidget);
    const int slot = takeSlot(prop, colorWidget);   // see addFloatProperty
    rowByName.insert(prop->name, colorWidget);

    connect(colorWidget->getPicker(), &ColorPickerWidget::onColorChanged, this,
           [this, slot](QColor value)
    {
        auto *p = propertyAt(slot);
        if (!p) return;
        p->setValue(QVariant(value));

        if (listener) {
            listener->onPropertyChanged(p);
        }

        emit onPropertyChanged(p);
    });

    // The popup session brackets the live changes above into one undo entry
    // (start fires before any change, so the listener records the old colour).
    connect(colorWidget->getPicker(), &ColorPickerWidget::pickingStarted, this, [this, slot]() {
        if (auto *p = propertyAt(slot)) if (listener) listener->onPropertyChangeStart(p);
    });
    connect(colorWidget->getPicker(), &ColorPickerWidget::pickingEnded, this, [this, slot]() {
        if (auto *p = propertyAt(slot)) if (listener) listener->onPropertyChangeEnd(p);
    });
}

void PropertyWidget::addBoolProperty(iris::Property *prop)
{
    auto boolProp = static_cast<iris::BoolProperty*>(prop);
    auto boolWidget = addCheckBox(boolProp->displayName);

    boolWidget->index = prop->id;
    boolWidget->setValue(boolProp->getValue().toBool());
    addRow(boolWidget);
    const int slot = takeSlot(prop, boolWidget);   // see addFloatProperty
    rowByName.insert(prop->name, boolWidget);

    connect(boolWidget, &CheckBoxWidget::valueChanged, this, [this, slot](bool value) {
        auto *p = propertyAt(slot);
        if (!p) return;
        // A checkbox toggle is one discrete gesture - one undo entry.
        if (listener) listener->onPropertyChangeStart(p);

        p->setValue(QVariant(value));

        if (listener) {
            listener->onPropertyChanged(p);
            listener->onPropertyChangeEnd(p);
        }

        emit onPropertyChanged(p);
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
    const int slot = takeSlot(prop, textureWidget);   // see addFloatProperty
    rowByName.insert(prop->name, textureWidget);

    connect(textureWidget, &TexturePickerWidget::valueChanged, this,
           [this, slot](QString value)
    {
        auto *p = propertyAt(slot);
        if (!p) return;
        // Picking a texture is a single discrete gesture: bracket it with
        // change start/end so it lands as one undo entry. Start must run
        // BEFORE the write - the listener records the old value from the prop.
        if (listener) listener->onPropertyChangeStart(p);

        p->setValue(QVariant(value));

        if (listener) {
            listener->onPropertyChanged(p);
            listener->onPropertyChangeEnd(p);
        }

        emit onPropertyChanged(p);
    });
}

void PropertyWidget::addFileProperty(iris::Property *prop)
{
    auto fileProp = static_cast<iris::FileProperty*>(prop);
    auto fileWidget = addFilePicker(fileProp->displayName, fileProp->suffix);

    fileWidget->index = prop->id;
    fileWidget->setFilepath(fileProp->getValue().toString());
    addRow(fileWidget);
    const int slot = takeSlot(prop, fileWidget);   // see addFloatProperty

    connect(fileWidget, &FilePickerWidget::onPathChanged, this, [this, slot](QString value) {
        auto *p = propertyAt(slot);
        if (!p) return;
        p->setValue(QVariant(value));

        if (listener) {
            listener->onPropertyChanged(p);
        }

        emit onPropertyChanged(p);
    });
}

void PropertyWidget::addVector2Property(iris::Property *prop)
{
	auto vecProp = static_cast<iris::Vec2Property*>(prop);
	auto widget = addVector2Widget(vecProp->displayName, vecProp->value.x(), vecProp->value.y());
	auto holder = addWidgetHolder(vecProp->displayName, widget);
	addRow(holder);
	const int slot = takeSlot(vecProp, widget);   // see addFloatProperty

	connect(widget, &Widget2D::valueChanged, this, [this, slot](iris::Vec2 value) {
		auto *p = static_cast<iris::Vec2Property *>(propertyAt(slot));
		if (!p) return;
		p->value = iris::toQt(value);
		if (listener) listener->onPropertyChanged(p);
		emit onPropertyChanged(p);
	});

}

void PropertyWidget::addVector3Property(iris::Property *prop)
{
	auto vecProp = static_cast<iris::Vec3Property*>(prop);
	auto widget = addVector3Widget(vecProp->displayName, vecProp->value.x(), vecProp->value.y(), vecProp->value.z());
	auto holder = addWidgetHolder(vecProp->displayName, widget);
	addRow(holder);
	const int slot = takeSlot(vecProp, widget);   // see addFloatProperty

	connect(widget, &Widget3D::valueChanged, this, [this, slot](iris::Vec3 value) {
		auto *p = static_cast<iris::Vec3Property *>(propertyAt(slot));
		if (!p) return;
		p->value = iris::toQt(value);
		if (listener) listener->onPropertyChanged(p);
		emit onPropertyChanged(p);
	});

}

void PropertyWidget::addVector4Property(iris::Property *prop)
{
	auto vecProp = static_cast<iris::Vec4Property*>(prop);
	auto widget = addVector4Widget(vecProp->displayName, vecProp->value.x(), vecProp->value.y(), vecProp->value.z(), vecProp->value.w());
	auto holder = addWidgetHolder(vecProp->displayName, widget);
	addRow(holder);
	const int slot = takeSlot(vecProp, widget);   // see addFloatProperty

	connect(widget, &Widget4D::valueChanged, this, [this, slot](iris::Vec4 value) {
		auto *p = static_cast<iris::Vec4Property *>(propertyAt(slot));
		if (!p) return;
		p->value = iris::toQt(value);
		if (listener) listener->onPropertyChanged(p);
		emit onPropertyChanged(p);
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

/// The row's slot: its index in this panel's property list. Recorded at
/// creation, used by every handler, and the thing rebind() re-points.
int PropertyWidget::takeSlot(iris::Property *prop, QWidget *valueRow)
{
    const int slot = properties.size();
    properties.append(prop);
    valueRows.append(valueRow);
    return slot;
}

iris::Property *PropertyWidget::propertyAt(int slot) const
{
    if (rebinding) return nullptr;      // a value being PUT INTO a row is not an edit
    return properties.value(slot, nullptr);
}

// CAN THESE ROWS SHOW THAT PROPERTY LIST? (ADD-1, 2026-09-15)
//
// A material's rows are decided entirely by its property list's SHAPE — the
// order, the types, the names, the labels, the ranges and an enum's vocabulary.
// Two materials with the same shape (every primitive in a scene, every default
// PbrMaterial) need the SAME widgets showing different numbers, and building a
// second set of them is what cost 44 ms of every mesh pick.
//
// The comparison is deliberately total: anything a row's CONSTRUCTION reads is
// compared, so a match means the rows would have been built identically. A
// mismatch falls back to the rebuild, which is always correct.
bool PropertyWidget::canRebind(const QList<iris::Property *> &props) const
{
    if (props.size() != properties.size() || props.isEmpty()) return false;
    for (int i = 0; i < props.size(); ++i) {
        const iris::Property *a = props.at(i);
        const iris::Property *b = properties.at(i);
        if (!a || !b || !valueRows.value(i)) return false;
        if (a->type != b->type || a->name != b->name || a->displayName != b->displayName)
            return false;
        switch (a->type) {
            case iris::PropertyType::Float: {
                auto *fa = static_cast<const iris::FloatProperty *>(a);
                auto *fb = static_cast<const iris::FloatProperty *>(b);
                if (fa->minValue != fb->minValue || fa->maxValue != fb->maxValue) return false;
                break;
            }
            case iris::PropertyType::Int: {
                auto *ia = static_cast<const iris::IntProperty *>(a);
                auto *ib = static_cast<const iris::IntProperty *>(b);
                if (ia->minValue != ib->minValue || ia->maxValue != ib->maxValue) return false;
                break;
            }
            case iris::PropertyType::List: {
                // The vocabulary IS the combo's item list (addEnumProperty).
                if (static_cast<const iris::ListProperty *>(a)->labels !=
                    static_cast<const iris::ListProperty *>(b)->labels) return false;
                break;
            }
            case iris::PropertyType::File: {
                if (static_cast<const iris::FileProperty *>(a)->suffix !=
                    static_cast<const iris::FileProperty *>(b)->suffix) return false;
                break;
            }
            default: break;
        }
    }
    return true;
}

// THE SAME ROWS, SHOWING SOMETHING ELSE.
//
// Nothing is destroyed and nothing is created: the property list is replaced,
// every row is told its new value, and the cross-row constraints are re-judged.
// `rebinding` makes the handlers inert while the values go in, so putting a
// number into a slider is not an edit of the material it came from.
//
// THE ROW REGISTRY SEES NOTHING AT ALL, which is the point: the rows are the
// same objects, still registered, still carrying their keys and their place in
// the section chain, so a live filter and the expand snapshot survive a pick
// (PROPERTY_FILTER_SPEC §6.2 — a retired row leaves the registry, and no row is
// retired here).
void PropertyWidget::rebind(const QList<iris::Property *> &props)
{
    Q_ASSERT(canRebind(props));
    rebinding = true;
    properties = props;
    rowByName.clear();
    for (int i = 0; i < props.size(); ++i) {
        iris::Property *prop = props.at(i);
        QWidget *row = valueRows.at(i);
        rowByName.insert(prop->name, row);
        switch (prop->type) {
            case iris::PropertyType::Float:
                static_cast<HFloatSliderWidget *>(row)->setValue(prop->getValue().toFloat());
                break;
            case iris::PropertyType::Int:
                static_cast<HFloatSliderWidget *>(row)->setValue(float(prop->getValue().toInt()));
                break;
            case iris::PropertyType::Color:
                static_cast<ColorValueWidget *>(row)->setColorValue(prop->getValue().value<QColor>());
                break;
            case iris::PropertyType::Bool:
                static_cast<CheckBoxWidget *>(row)->setValue(prop->getValue().toBool());
                break;
            case iris::PropertyType::Texture:
                static_cast<TexturePickerWidget *>(row)->setTexture(prop->getValue().toString());
                break;
            case iris::PropertyType::File:
                static_cast<FilePickerWidget *>(row)->setFilepath(prop->getValue().toString());
                break;
            case iris::PropertyType::List:
                static_cast<ComboBoxWidget *>(row)->setCurrentIndex(prop->getValue().toInt());
                break;
            case iris::PropertyType::Vec2: {
                const QVector2D v = static_cast<iris::Vec2Property *>(prop)->value;
                static_cast<Widget2D *>(row)->setValues(v.x(), v.y());
                break;
            }
            case iris::PropertyType::Vec3: {
                const QVector3D v = static_cast<iris::Vec3Property *>(prop)->value;
                static_cast<Widget3D *>(row)->setValues(v.x(), v.y(), v.z());
                break;
            }
            case iris::PropertyType::Vec4: {
                const QVector4D v = static_cast<iris::Vec4Property *>(prop)->value;
                static_cast<Widget4D *>(row)->setValues(v.x(), v.y(), v.z(), v.w());
                break;
            }
            case iris::PropertyType::None:
            default: break;
        }
    }
    rebinding = false;
    applyRowConstraints();
}

void PropertyWidget::setProperties(QList<iris::Property*> properties)
{
    rowByName.clear();
    this->properties.clear();
    valueRows.clear();
    for (auto prop : properties) {
        // THE ROW'S KEY IS THE PROPERTY'S NAME (PROPERTY_FILTER_SPEC §3.2).
        // Set here rather than in ten adders: addRow() is the one place the row
        // reaches the registry, and every branch below goes through it exactly
        // once. `roughness` finds the row whose displayName is "Roughness".
        pendingKey = prop->name;
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
        pendingKey.clear();
    }

    updatePane();

    // `this->properties` was filled slot by slot as the rows were built
    // (takeSlot) — it is the same list, in the same order.
    Q_ASSERT(this->properties.size() == properties.size());
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

    // ---- 1b. the WORKFLOW (MATERIAL_GAPS_SPEC GAP 1) ----
    // The renderer's metalness and F0 are THE SAME FLOAT in the datablock, so
    // exactly one of them is live per material and the other reaches nothing.
    // Grey the dead half rather than let a user drag it. Values are kept, so
    // switching workflow brings them back.
    //
    // The workflow also RENAMES a map row: PBSM_METALLIC and PBSM_SPECULAR are
    // one renderer texture unit reinterpreted by the workflow, so the document's
    // single `metallicMap` row means metalness in one workflow and specular in
    // the other two (I-4). One table (sharedMapDisplayName) names it, the same
    // table that decides the map's colour space.
    bool haveWorkflow = false;
    const int workflow = valueOf(QStringLiteral("workflow"), &haveWorkflow);
    if (haveWorkflow && !unlit) {
        const bool metallic = workflow == 0;
        const QString whyMetallic =
            tr("Not used by the Metallic workflow — metalness and the fresnel term are the "
               "same value in the renderer. The value is kept and returns with the workflow.");
        const QString whySpecular =
            tr("Not used by a Specular workflow — the specular map and fresnel replace "
               "metalness. The value is kept and returns with the workflow.");
        for (const QString &name : iris::PbrMaterial::rowsUnusedWhenMetallic())
            constrain(name, !metallic, whyMetallic);
        for (const QString &name : iris::PbrMaterial::rowsUnusedWhenSpecular())
            constrain(name, metallic, whySpecular);
        // F0 comes from EITHER the IOR or the colour, never both.
        bool haveUseFresnel = false;
        const bool useColour = valueOf(QStringLiteral("useFresnelColor"), &haveUseFresnel) != 0;
        if (haveUseFresnel && !metallic) {
            constrain(QStringLiteral("ior"), !useColour,
                      tr("Fresnel Color is supplying F0 directly; turn it off to author by IOR."));
            constrain(QStringLiteral("fresnelColor"), useColour,
                      tr("F0 is computed from the Index of Refraction; turn on Use Fresnel Color "
                         "to author it directly."));
        }
        if (auto *w = qobject_cast<TexturePickerWidget *>(
                rowByName.value(QStringLiteral("metallicMap"))))
            w->ui->label->setText(
                QString::fromLatin1(iris::PbrMaterial::sharedMapDisplayName(workflow)));
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

/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROPERTYWIDGET_H
#define PROPERTYWIDGET_H

#include <QWidget>
#include "irisgl/core/properties/property.h"
#include "modules/materials/propertywidgets/propertywidgetbase.h"

namespace Ui {
    class PropertyWidget;
}

class HFloatSliderWidget;
class ColorValueWidget;
class CheckBoxWidget;
class TexturePickerWidget;
class FilePickerWidget;

class BaseWidget;

class Project;

class PropertyWidget : public QWidget, iris::PropertyListener
{
    Q_OBJECT

public:
    explicit PropertyWidget(QWidget *parent = 0);
    ~PropertyWidget();

    /// The one live Project (Phase 4: was the Globals::project static). Set by
    /// AccordianBladeWidget::addPropertyWidget(); forwarded to the texture
    /// pickers this widget builds.
    Project *project = nullptr;

    void addProperty(const iris::Property*);
    void setProperties(QList<iris::Property*>);
    QList<iris::Property*> getProperties() { return properties; }
    int getHeight();

    HFloatSliderWidget  *addFloatValueSlider(const QString&, float min, float max);
    ColorValueWidget    *addColorPicker(const QString&);
    CheckBoxWidget      *addCheckBox(const QString&);
    TexturePickerWidget *addTexturePicker(const QString&);
    FilePickerWidget    *addFilePicker(const QString &name, const QString &suffix);
	Widget2D*			addVector2Widget(const QString&, float xValue, float yValue);
	Widget3D*			addVector3Widget(const QString&, float xValue, float yValue, float zValue);
	Widget4D*			addVector4Widget(const QString&, float xValue, float yValue, float zValue, float wValue);
	QWidget*			addWidgetHolder(QString title, QWidget *widget);

    void addFloatProperty(iris::Property*);
    void addIntProperty(iris::Property*);
    /// The generic ENUM row: a labeled dropdown built from ListProperty's own
    /// `labels`, combo index == stored value. Every enum row in the app goes
    /// through here — there is deliberately no second by-name special case.
    void addEnumProperty(iris::Property*);
    void addColorProperty(iris::Property*);
    void addBoolProperty(iris::Property*);
    void addTextureProperty(iris::Property*);
    void addFileProperty(iris::Property*);

	void addVector2Property(iris::Property*);
	void addVector3Property(iris::Property*);
	void addVector4Property(iris::Property*);

    void setListener(iris::PropertyListener*);

signals:
    void onPropertyChanged(iris::Property*);
    void onPropertyChangeStart(iris::Property*);
    void onPropertyChangeEnd(iris::Property*);

private:
    QList<iris::Property*> properties;
    iris::PropertyListener *listener;
    int progressiveHeight, stretch;

    /// The row widget each property built, by property name. Only needed by
    /// rows that CONSTRAIN other rows (see applyRowConstraints).
    QHash<QString, QWidget*> rowByName;

    /// Cross-row availability. One rule today: the two clear-coat rows are
    /// disabled unless the material's BRDF is in the Default family, because
    /// the renderer cannot carry a coat on any other one. Disabling (rather
    /// than zeroing) is the decided behaviour — the authored coat survives a
    /// round trip through another BRDF (HLMS_ADOPTION_SPEC D-P1b).
    void applyRowConstraints();

    void updatePane();

    Ui::PropertyWidget *ui;
};

#endif // PROPERTYWIDGET_H

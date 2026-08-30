/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ACCORDIANBLADEWIDGET_H
#define ACCORDIANBLADEWIDGET_H

#include <QWidget>

namespace Ui {
    class AccordianBladeWidget;
}

class TransformEditor;
class ColorValueWidget;
class TexturePickerWidget;
class HFloatSliderWidget;
class CheckBoxWidget;
class ComboBoxWidget;
class TextInputWidget;
class LabelWidget;
class FilePickerWidget;
class CubeMapWidget;
// class PropertyWidget;
#include "ui/panels/propertywidget.h"
#include "modules/materials/propertywidgets/propertywidgetbase.h"

#include <QLayout>

class Project;

class AccordianBladeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AccordianBladeWidget(QWidget* parent = 0);
    ~AccordianBladeWidget();

    TransformEditor*        addTransformControls();

    ColorValueWidget*       addColorPicker(const QString&);
    TexturePickerWidget*    addTexturePicker(const QString&);
    HFloatSliderWidget*     addFloatValueSlider(const QString&, float start, float end, float value = 0.f);
    CheckBoxWidget*         addCheckBox(const QString&, bool value = false);
    ComboBoxWidget*         addComboBox(const QString&);
    TextInputWidget*        addTextInput(const QString&);
    LabelWidget*            addLabel(const QString&, const QString&);
    FilePickerWidget*       addFilePicker(const QString&);
	Widget2D*				addVector2Widget(const QString&, float xValue, float yValue);
	Widget3D*				addVector3Widget(const QString&, float xValue, float yValue, float zValue);
	Widget4D*				addVector4Widget(const QString&, float xValue, float yValue, float zValue, float wValue);
	CubeMapWidget*			addCubeMapWidget(QStringList list);
	CubeMapWidget*			addCubeMapWidget();
	CubeMapWidget*			addCubeMapWidget(QString top, QString bottom, QString left, QString front, QString right, QString back);

    PropertyWidget*         addPropertyWidget();

    /// The one live Project (Phase 4: was the Globals::project static). Set by
    /// whoever creates the panel; the add*() helpers above forward it to the
    /// controls they build, which read it in their drop handlers.
    Project *project = nullptr;
    virtual void setProject(Project *p) { project = p; }

    void setPanelTitle(const QString&);
    void collapse();
    void expand();

    void clearPanel(QLayout *layout);
    int minimum_height, stretch;

    void stepHeight(int h) {
        this->minimum_height += h;
    }

    void resetHeight() {
        this->minimum_height = 0;
    }

    void setHeight(int h) {
        this->minimum_height = h;
    }

private slots:
    void onPanelToggled();

private:
    Ui::AccordianBladeWidget *ui;
};

#endif // ACCORDIANBLADEWIDGET_H

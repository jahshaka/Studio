/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

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
#include "propertywidget.h"
#include "src/shadergraph//propertywidgets/propertywidgetbase.h"

#include <QLayout>

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

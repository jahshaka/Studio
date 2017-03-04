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
class TexturePicker;
class HFloatSlider;
class CheckBoxProperty;
class ComboBox;
class TextInput;
class LabelWidget;

class AccordianBladeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AccordianBladeWidget(QWidget* parent = 0);
    ~AccordianBladeWidget();

    TransformEditor*    addTransformControls();

    ColorValueWidget*   addColorPicker(const QString&);
    TexturePicker*      addTexturePicker(const QString&);
    HFloatSlider*       addFloatValueSlider(const QString&, float start, float end);
    CheckBoxProperty*   addCheckBox(const QString&, bool value = false);
    ComboBox*           addComboBox(const QString&);
    TextInput*          addTextInput(const QString&);
    LabelWidget*        addLabel(const QString& title, const QString& text);

    void setPanelTitle(const QString&);
    void expand();

private slots:
    void onPanelToggled();

private:
    Ui::AccordianBladeWidget *ui;
    int minimum_height, stretch;
};

#endif // ACCORDIANBLADEWIDGET_H

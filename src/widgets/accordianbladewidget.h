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
    HFloatSliderWidget*     addFloatValueSlider(const QString&, float start, float end);
    CheckBoxWidget*         addCheckBox(const QString&, bool value = false);
    ComboBoxWidget*         addComboBox(const QString&);
    TextInputWidget*        addTextInput(const QString&);
    LabelWidget*            addLabel(const QString&, const QString&);
    FilePickerWidget*       addFilePicker(const QString&);

    void setPanelTitle(const QString&);
    void expand();

    void clearPanel(QLayout *layout);
    int minimum_height, stretch;

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

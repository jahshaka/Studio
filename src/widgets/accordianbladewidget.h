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

/*
 *  Widget's are appended with 1 of 3 types
 *  1. Property     - code representation of widgets
 *  2. Widget       - actual form with Qt/custom widgets
 *  3. Pane         - a dynamic layout that houses widgets
 *
 */

class AccordianBladeWidget : public QWidget
{
    Q_OBJECT

public:
    int minimum_height;

    explicit AccordianBladeWidget(QWidget *parent = 0);
    ~AccordianBladeWidget();

    TransformEditor* addTransform();


    void addCheckBoxOption( QString name);
    void setMaxHeight( int height );
    void addFilePicker( QString name, QString fileextention );
    void setContentTitle( QString title );


    ColorValueWidget* addColorPicker( QString name );
    TexturePicker* addTexturePicker( QString name );


    HFloatSlider* addFloatValueSlider( QString name, float range_1 , float range_2 );
    CheckBoxProperty* addCheckBox( QString name, bool value = false );
    ComboBox* addComboBox(QString name);

    TextInput* addTextInput(QString name);
    LabelWidget* addLabel(QString name);

    void expand();
    void expand2();

    HFloatSlider* addValueSlider(QString label, float start, float end);

private slots:
    void on_toggle_clicked();

private:
    Ui::AccordianBladeWidget *ui;
};

#endif // ACCORDIANBLADEWIDGET_H

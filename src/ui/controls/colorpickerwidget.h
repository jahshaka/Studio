/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef COLORPICKERWIDGET_H
#define COLORPICKERWIDGET_H

#include <QWidget>
//#include <QColorDialog>

namespace Ui {
class ColorPickerWidget;
}
class QColorDialog;
class ColorView;
class ColorPickerWidget : public QWidget
{
    Q_OBJECT

    QColor color;
	ColorView *view;
    QColorDialog* dialog;

public:
    int index;
    explicit ColorPickerWidget(QWidget *parent = 0);
    ~ColorPickerWidget();

    void paintEvent(QPaintEvent* evt);
    void mouseReleaseEvent(QMouseEvent * event);
    void mousePressEvent(QMouseEvent * event);
    QColor getColor()
    {
        return color;
    }

public slots:
    void setColor(QColor col);

private slots:
    void colorChanged(QColor color);

signals:
    void onSetColor(QColor col);
    void onColorChanged(QColor color);
    // The colour-view popup session: started when the popup opens (before any
    // live change), ended when it closes. Lets a listener treat the whole pick
    // as one undoable gesture while onColorChanged streams live previews.
    void pickingStarted();
    void pickingEnded();

private:
    Ui::ColorPickerWidget *ui;
};

#endif // COLORPICKERWIDGET_H

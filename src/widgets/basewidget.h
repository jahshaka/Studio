#ifndef BASEWIDGET_H
#define BASEWIDGET_H

#include <QWidget>

enum WidgetType
{
    IntWidget,
    FloatWidget,
    BoolWidget,
    ColorWidget,
    TextureWidget,
    FileWidget
};

class BaseWidget : public QWidget
{
public:
    BaseWidget(QWidget *parent = nullptr) : QWidget(parent) {

    }

    // unsigned id;
    WidgetType type;
};

#endif // BASEWIDGET_H

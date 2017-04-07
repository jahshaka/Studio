#ifndef PROPERTYWIDGET_H
#define PROPERTYWIDGET_H

#include <QWidget>
#include "../irisgl/src/materials/propertytype.h"

namespace Ui {
    class PropertyWidget;
}

class HFloatSliderWidget;
class ColorValueWidget;
class CheckBoxWidget;
class TexturePickerWidget;
class FilePickerWidget;

class PropertyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PropertyWidget(QWidget *parent = 0);
    ~PropertyWidget();

    void addProperty(const Property*);
    void setProperties(QList<Property*>);
    void update();
    int getHeight();

    HFloatSliderWidget  *addFloatValueSlider(const QString&, float start, float end);
    ColorValueWidget    *addColorPicker(const QString&);
    CheckBoxWidget      *addCheckBox(const QString& title);
    TexturePickerWidget *addTexturePicker(const QString& name);
    FilePickerWidget    *addFilePicker(const QString &name);

private:
    Ui::PropertyWidget *ui;
    QList<Property*> properties;
    int minimum_height, stretch, num_props;
};

#endif // PROPERTYWIDGET_H

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

class BaseWidget;

class PropertyWidget : public QWidget, PropertyListener
{
    Q_OBJECT

public:
    explicit PropertyWidget(QWidget *parent = 0);
    ~PropertyWidget();

    void addProperty(const Property*);
    void setProperties(QList<Property*>);
    int getHeight();

    HFloatSliderWidget  *addFloatValueSlider(const QString&, float min, float max);
    ColorValueWidget    *addColorPicker(const QString&);
    CheckBoxWidget      *addCheckBox(const QString&);
    TexturePickerWidget *addTexturePicker(const QString&);
    FilePickerWidget    *addFilePicker(const QString &name, const QString &suffix);

    void addFloatProperty(Property*);
    void addIntProperty(Property*);
    void addColorProperty(Property*);
    void addBoolProperty(Property*);
    void addTextureProperty(Property*);
    void addFileProperty(Property*);

    void setListener(PropertyListener*);

signals:
    void onPropertyChanged(Property*);

private:
    QList<Property*> properties;
    PropertyListener *listener;
    int progressiveHeight, stretch;

    void updatePane();

    Ui::PropertyWidget *ui;
};

#endif // PROPERTYWIDGET_H

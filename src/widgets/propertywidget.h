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

class PropertyWidget : public QWidget, iris::PropertyListener
{
    Q_OBJECT

public:
    explicit PropertyWidget(QWidget *parent = 0);
    ~PropertyWidget();

    void addProperty(const iris::Property*);
    void setProperties(QList<iris::Property*>);
    QList<iris::Property*> getProperties() { return properties; }
    int getHeight();

    HFloatSliderWidget  *addFloatValueSlider(const QString&, float min, float max);
    ColorValueWidget    *addColorPicker(const QString&);
    CheckBoxWidget      *addCheckBox(const QString&);
    TexturePickerWidget *addTexturePicker(const QString&);
    FilePickerWidget    *addFilePicker(const QString &name, const QString &suffix);

    void addFloatProperty(iris::Property*);
    void addIntProperty(iris::Property*);
    void addColorProperty(iris::Property*);
    void addBoolProperty(iris::Property*);
    void addTextureProperty(iris::Property*);
    void addFileProperty(iris::Property*);

    void setListener(iris::PropertyListener*);

signals:
    void onPropertyChanged(iris::Property*);

private:
    QList<iris::Property*> properties;
    iris::PropertyListener *listener;
    int progressiveHeight, stretch;

    void updatePane();

    Ui::PropertyWidget *ui;
};

#endif // PROPERTYWIDGET_H

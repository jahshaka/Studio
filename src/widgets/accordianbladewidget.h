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
    ColorValueWidget* addColorPicker( QString name );
    TexturePicker* addTexturePicker( QString name );
    void setContentTitle( QString title );
    HFloatSlider* addFloatValueSlider( QString name, float range_1 , float range_2 );

    void expand();

private slots:
    void on_toggle_clicked();

private:
    Ui::AccordianBladeWidget *ui;
};

#endif // ACCORDIANBLADEWIDGET_H

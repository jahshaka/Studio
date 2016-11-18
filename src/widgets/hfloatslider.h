#ifndef HFLOATSLIDER_H
#define HFLOATSLIDER_H

#include <QWidget>

namespace Ui {
class HFloatSlider;
}

class HFloatSlider : public QWidget
{
    Q_OBJECT

    int precision;
    float value;

    float minVal;
    float maxVal;
public:
    explicit HFloatSlider(QWidget *parent = 0);
    ~HFloatSlider();
    Ui::HFloatSlider *ui;

    float getValue();
    void setValue( float value );

    void setRange(float min,float max);

signals:
    void valueChanged(float value);


private slots:
    void onValueSliderChanged(int value);
    void onValueSpinboxChanged(double value);
};

#endif // HFLOATSLIDER_H

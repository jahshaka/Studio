#include "accordianbladewidget.h"
#include "ui_accordianbladewidget.h"
#include "hfloatslider.h"
#include "ui_hfloatslider.h"
#include <QSpinBox>

AccordianBladeWidget::AccordianBladeWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AccordianBladeWidget)
{
    ui->setupUi(this);
    QVBoxLayout *layout = new QVBoxLayout();
    ui->contentpane->setLayout(layout);
    ui->contentpane->setVisible(false);
}

AccordianBladeWidget::~AccordianBladeWidget()
{
    delete ui;
}

void AccordianBladeWidget::on_toggle_clicked()
{
    bool state = ui->contentpane->isVisible();

    if( state == true )
    {
        ui->contentpane->setVisible(false);
    }
    else
    {
        ui->contentpane->setVisible(true);
    }
}

void AccordianBladeWidget::setContentTitle( QString title ){

    ui->content_title->setText(title);

}

void AccordianBladeWidget::addFloatValueSlider( QString name, float range_1 , float range_2 )
{
    HFloatSlider *slider = new HFloatSlider();
    slider->ui->label->setText(name);
    slider->ui->slider->setRange( range_1 , range_2 );
    slider->ui->spinbox->setRange( range_1 , range_2 );

    ui->contentpane->layout()->addWidget(slider);
}

/*
void Accordian::addColorPicker( QString name )
{

    ColorPicker *colorpicker = new ColorPicker();
    colorpicker->ui->label->setText(name);
    ui->contentpane->layout()->addWidget(colorpicker);

}
*/
/*
void Accordian::addTexturePicker( QString name ){

    TexturePicker *texpicker = new TexturePicker();
    texpicker->ui->label->setText("diffuse texture");
    ui->contentpane->layout()->addWidget(texpicker);
}
*/

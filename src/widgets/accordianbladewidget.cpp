#include "accordianbladewidget.h"
#include "ui_accordianbladewidget.h"
#include "hfloatslider.h"
#include "ui_hfloatslider.h"
#include "colorvaluewidget.h"
#include "texturepicker.h"
#include "ui_texturepicker.h"
#include "transformeditor.h"
#include <QSpinBox>

AccordianBladeWidget::AccordianBladeWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AccordianBladeWidget)
{
    ui->setupUi(this);
    QVBoxLayout *layout = new QVBoxLayout();
    ui->contentpane->setLayout(layout);
    ui->contentpane->setVisible(false);
    this->setMinimumHeight( 30 );
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
        this->setMinimumHeight( 30 );
    }
    else
    {
        ui->contentpane->setVisible(true);
        this->setMinimumHeight( this->maximumHeight() );
    }
}

void AccordianBladeWidget::setMaxHeight( int height){
    this->setMaximumHeight( height );
}

void AccordianBladeWidget::setContentTitle( QString title ){

    ui->content_title->setText(title);

}

void AccordianBladeWidget::addTransform(){
    TransformEditor *transform = new TransformEditor();
    ui->contentpane->layout()->addWidget(transform);
}

void AccordianBladeWidget::addColorPicker( QString name )
{

    ColorValueWidget *colorpicker = new ColorValueWidget();
    int height = colorpicker->height();
    minimum_height += height;
    colorpicker->setLabel(name);
    ui->contentpane->layout()->addWidget(colorpicker);

}

void AccordianBladeWidget::addTexturePicker( QString name ){

    TexturePicker *texpicker = new TexturePicker();
    texpicker->ui->label->setText(name);
    ui->contentpane->layout()->addWidget(texpicker);
}

void AccordianBladeWidget::addFloatValueSlider( QString name, float range_1 , float range_2 )
{
    HFloatSlider *slider = new HFloatSlider();
    int height = slider->height();
    minimum_height = minimum_height + height;

    slider->ui->label->setText(name);
    slider->ui->slider->setRange( range_1 , range_2 );
    slider->ui->spinbox->setRange( range_1 , range_2 );

    ui->contentpane->layout()->addWidget(slider);

}


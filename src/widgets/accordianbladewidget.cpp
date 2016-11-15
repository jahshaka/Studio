#include "accordianbladewidget.h"
#include "ui_accordianbladewidget.h"
#include "hfloatslider.h"
#include "ui_hfloatslider.h"
#include "colorvaluewidget.h"
#include "texturepicker.h"
#include "ui_texturepicker.h"
#include "transformeditor.h"
#include "filepickerwidget.h"
#include "ui_filepickerwidget.h"
#include <QSpinBox>

AccordianBladeWidget::AccordianBladeWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AccordianBladeWidget)
{
    ui->setupUi(this);
    this->setMinimumHeight( 30 );
    QVBoxLayout *layout = new QVBoxLayout();
    ui->contentpane->setLayout(layout);
    ui->contentpane->setVisible(false);
    ui->toggle->setStyleSheet("QPushButton { border-image: url(:/app/icons/right-chevron.svg); } QPushButton:hover { background-color: rgb(30, 144, 255); border: none;}");
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
        ui->toggle->setStyleSheet("QPushButton { border-image: url(:/app/icons/right-chevron.svg); } QPushButton:hover { background-color: rgb(30, 144, 255); border: none;}");
        ui->contentpane->setVisible(false);
        this->setMinimumHeight( 30 );
    }
    else
    {
        ui->toggle->setStyleSheet("QPushButton { border-image: url(:/app/icons/chevron-arrow-down.svg); } QPushButton:hover { background-color: rgb(30, 144, 255); border: none;}");
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

TransformEditor* AccordianBladeWidget::addTransform(){
    TransformEditor *transform = new TransformEditor();
    ui->contentpane->layout()->addWidget(transform);

    return transform;
}

ColorValueWidget* AccordianBladeWidget::addColorPicker( QString name )
{

    ColorValueWidget *colorpicker = new ColorValueWidget();
    int height = colorpicker->height();
    minimum_height += height;
    colorpicker->setLabel(name);
    ui->contentpane->layout()->addWidget(colorpicker);

    return colorpicker;
}

void AccordianBladeWidget::addFilePicker( QString name  ){
    FilePickerWidget *filepicker = new FilePickerWidget();
    filepicker->ui->label->setText(name);
    ui->contentpane->layout()->addWidget(filepicker);
}

TexturePicker* AccordianBladeWidget::addTexturePicker( QString name ){

    TexturePicker *texpicker = new TexturePicker();
    texpicker->ui->label->setText(name);
    ui->contentpane->layout()->addWidget(texpicker);

    return texpicker;
}

HFloatSlider* AccordianBladeWidget::addFloatValueSlider( QString name, float range_1 , float range_2 )
{
    HFloatSlider *slider = new HFloatSlider();
    int height = slider->height();
    minimum_height = minimum_height + height;

    slider->ui->label->setText(name);
    slider->ui->slider->setRange( range_1 , range_2 );
    slider->ui->spinbox->setRange( range_1 , range_2 );

    ui->contentpane->layout()->addWidget(slider);

    return slider;
}

void AccordianBladeWidget::expand()
{
    ui->contentpane->setVisible(true);
    this->setMinimumHeight( this->maximumHeight() );
}

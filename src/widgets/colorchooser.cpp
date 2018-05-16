#include "colorchooser.h"
#include <qdebug.h>
#include <qpainter.h>
#include <qcursor.h>
#include "colorcircle.h"
#include <qrgb.h>
#include <qlayout.h>
#include <QVBoxLayout>
#include <qdesktopwidget.h>
#include <qapplication.h>


ColorChooser::ColorChooser(QWidget *parent) :
    QWidget(parent)
{
    
    setWindowFlags(Qt::FramelessWindowHint |Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
    setAttribute(Qt::WA_TranslucentBackground);
    desktop = new QDesktopWidget;

    setContentsMargins(0,0,0,0);
    setGeometry(desktop->width()/2 - 150,desktop->height()/2 - 200,300,410);

    x = geometry().x();
    y = geometry().y();
    gWidth = geometry().width();
    gHeight = geometry().height();
    cbg = new CustomBackground(this);

    configureDisplay();
    setColorBackground();
    setConnections();
    setStyleForApplication();
    color = color.fromRgb(255,255,255);
    setSliders(color);
    alphaSpin->setValue(1.0);


}

ColorChooser::~ColorChooser()
{
   
}

void ColorChooser::changeBackgroundColorOfDisplayWidgetHsv()
{

    color.setHsv(hSpin->value(),sSpin->value(), vSpin->value());

    circlebg->drawSmallCircle(color);
    setValueInColor();
    setRgbSliders(color);


}
void ColorChooser::changeBackgroundColorOfDisplayWidgetRgb()
{
    color.setRgb(rSpin->value(),gSpin->value(), bSpin->value());
    circlebg->drawSmallCircle(color);
    setValueInColor();
    setHsvSliders(color);
}

void ColorChooser::configureDisplay()
{
    groupBox = new QGroupBox();
    auto mainLayout = new QGridLayout();
    //mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    mainLayout->addWidget(groupBox);
    setLayout(mainLayout);

    QVBoxLayout *vLayout = new QVBoxLayout();
    QVBoxLayout *rgbLayout= new QVBoxLayout();
    QVBoxLayout *hsvLayout = new QVBoxLayout();
    QHBoxLayout *colorLayout = new QHBoxLayout();
    QHBoxLayout *hexLayout = new QHBoxLayout();
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QHBoxLayout *finalButtonLayout = new QHBoxLayout();
    QHBoxLayout *rSliderSpin = new QHBoxLayout();
    QHBoxLayout *gSliderSpin = new QHBoxLayout();
    QHBoxLayout *bSliderSpin = new QHBoxLayout();
    QHBoxLayout *hSliderSpin = new QHBoxLayout();
    QHBoxLayout *sSliderSpin = new QHBoxLayout();
    QHBoxLayout *vSliderSpin = new QHBoxLayout();
    QHBoxLayout *alphaSliderLayout = new QHBoxLayout();
    groupBox->setLayout(vLayout);

    rgbLayout->setSpacing(0);
    hsvLayout->setSpacing(0);

    colorDisplay = new QWidget();
    rgbHolder = new QWidget();
    rgbHolder->setLayout(rgbLayout);

    hsvHolder= new QWidget();
    hsvHolder->setLayout(hsvLayout);

    hexHolder= new QWidget();
    hexEdit = new QLineEdit();
    hexHolder->setLayout(hexLayout);
    hexLayout->addWidget(hexEdit);

    cancel = new QPushButton("cancel");
    select = new QPushButton("Select");
    picker = new QPushButton("pick");
    picker->setCheckable(true);
    picker->setChecked(false);
    rgbBtn = new QPushButton("RGB");
    rgbBtn->setCheckable(true);
    rgbBtn->setChecked(true);
    rgbBtn->setAutoExclusive(true);
    hex = new QPushButton("HEX");
    hex->setCheckable(true);
    hex->setAutoExclusive(true);
    hsv = new QPushButton("HSV");
    hsv->setCheckable(true);
    hsv->setAutoExclusive(true);
    buttonLayout->addWidget(rgbBtn);
    buttonLayout->addWidget(hsv);
    buttonLayout->addWidget(hex);
    buttonLayout->addWidget(picker);
    finalButtonLayout->addWidget(cancel);
    finalButtonLayout->addWidget(select);


    stackHolder = new QStackedWidget();

    redSlider = new CustomSlider(Qt::Horizontal);
    greenSlider = new CustomSlider(Qt::Horizontal);
    blueSlider = new CustomSlider(Qt::Horizontal);
    hueSlider = new CustomSlider(Qt::Horizontal);
    saturationSlider = new CustomSlider(Qt::Horizontal);
    valueSlider = new CustomSlider(Qt::Horizontal);
    alphaSlider = new CustomSlider(Qt::Horizontal);
    adjustSlider = new CustomSlider(Qt::Vertical);

    alphaSlider->setObjectName(QStringLiteral("alphaSlider"));
    //adjustSlider->setInvertedAppearance(true);
    connect(valueSlider,&CustomSlider::sliderMoved,[=](){
        adjustSlider->setValue(valueSlider->sliderPosition());
    });
    connect(adjustSlider,&CustomSlider::sliderMoved,[=](){
        valueSlider->setValue(adjustSlider->sliderPosition());
    });
    connect(valueSlider,&CustomSlider::sliderPressed,[=](){
        adjustSlider->setValue(valueSlider->sliderPosition());
    });
    connect(adjustSlider,&CustomSlider::sliderPressed,[=](){
        valueSlider->setValue(adjustSlider->sliderPosition());
    });
    rSpin = new QSpinBox();
    rSpin->setRange(0,255);
    gSpin = new QSpinBox();
    gSpin->setRange(0,255);
    bSpin = new QSpinBox();
    bSpin->setRange(0,255);
    hSpin = new QSpinBox();
    hSpin->setRange(0,360);
    sSpin = new QSpinBox();
    sSpin->setRange(0,255);
    vSpin = new QSpinBox();
    vSpin->setRange(0,255);
    alphaSpin = new QDoubleSpinBox();
    alphaSpin->setRange(0.0,1.0);
    alphaSpin->setSingleStep(0.1);

    redSlider->setMinLabel(QString("R:"));
    greenSlider->setMinLabel(QString("G:"));
    blueSlider->setMinLabel(QString("B:"));
    hueSlider->setMinLabel(QString("H:"));
    saturationSlider->setMinLabel(QString("S:"));
    valueSlider->setMinLabel(QString("V:"));
    alphaSlider->setMinLabel(QString("Alpha"));
    redSlider->setMaximum(255);
    greenSlider->setMaximum(255);
    blueSlider->setMaximum(255);
    saturationSlider->setMaximum(255);
    valueSlider->setMaximum(255);
    adjustSlider->setRange(0,255);
    hueSlider->setMaximum(360);
    alphaSlider->setRange(0,255);

    adjustSlider->setSliderPosition(255);

    colorLayout->addSpacing(20);
    colorLayout->addWidget(colorDisplay);
    colorLayout->addWidget(adjustSlider);
    colorLayout->addSpacing(40);

    rSliderSpin->addWidget(redSlider);
    rSliderSpin->addWidget(rSpin);
    gSliderSpin->addWidget(greenSlider);
    gSliderSpin->addWidget(gSpin);
    bSliderSpin->addWidget(blueSlider);
    bSliderSpin->addWidget(bSpin);
    hSliderSpin->addWidget(hueSlider);
    hSliderSpin->addWidget(hSpin);
    sSliderSpin->addWidget(saturationSlider);
    sSliderSpin->addWidget(sSpin);
    vSliderSpin->addWidget(valueSlider);
    vSliderSpin->addWidget(vSpin);

    rgbLayout->addLayout(rSliderSpin);
    rgbLayout->addLayout(gSliderSpin);
    rgbLayout->addLayout(bSliderSpin);
    hsvLayout->addLayout(hSliderSpin);
    hsvLayout->addLayout(sSliderSpin);
    hsvLayout->addLayout(vSliderSpin);




    //    rgbLayout->addWidget(redSlider);
    //    rgbLayout->addWidget(greenSlider);
    //    rgbLayout->addWidget(blueSlider);
    //    hsvLayout->addWidget(hueSlider);
    //    hsvLayout->addWidget(saturationSlider);
    //    hsvLayout->addWidget(valueSlider);


    stackHolder->addWidget(rgbHolder);
    stackHolder->addWidget(hsvHolder);
    stackHolder->addWidget(hexHolder);


    // vLayout->addWidget(mainHolder);
    // vLayout->addWidget(buttonHolder);

    alphaSliderLayout->addSpacing(10);
    alphaSliderLayout->addWidget(alphaSlider);
    alphaSliderLayout->addWidget(alphaSpin);
    alphaSliderLayout->addSpacing(10);
    alphaSliderLayout->setSpacing(0);

    vLayout->addLayout(colorLayout);
    vLayout->addSpacing(15);
    vLayout->addLayout(buttonLayout);
    vLayout->addWidget(stackHolder);
    vLayout->addLayout(alphaSliderLayout);
    vLayout->addLayout(finalButtonLayout);
    groupBox->setGeometry(0,0,gWidth,gHeight);
}

void ColorChooser::setConnections()
{
    connect(rgbBtn, &QPushButton::pressed, [this](){
        if(picker->isChecked())
            exitPickerMode();
        stackHolder->setCurrentIndex(0);
    });
    connect(hsv, &QPushButton::pressed, [this](){
        if(picker->isChecked())
            exitPickerMode();
        stackHolder->setCurrentIndex(1);
    });
    connect(hex, &QPushButton::pressed, [this](){
        if(picker->isChecked())
            exitPickerMode();
        stackHolder->setCurrentIndex(2);
    });
    connect(cancel, &QPushButton::pressed, [this]() {
		hide();
    });
    connect(select, &QPushButton::pressed, [this]() {
		hide();
    });

    connect(picker, SIGNAL(toggled(bool)),this,SLOT(pickerMode(bool)));
    connect(cbg,SIGNAL(finished(bool)),this,SLOT(pickerMode(bool)));
    connect(cbg,SIGNAL(positionChanged(QColor)),this,SLOT(setSliders(QColor)));

    connect(hueSlider, &CustomSlider::sliderPressed,[=](){
        hSpin->setValue(hueSlider->sliderPosition());
    });
    connect(saturationSlider, &CustomSlider::sliderPressed,[=](){
        sSpin->setValue(saturationSlider->sliderPosition());
    });
    connect(valueSlider, &CustomSlider::sliderPressed,[=](){
        vSpin->setValue(valueSlider->sliderPosition());
    });
    connect(adjustSlider, &CustomSlider::sliderPressed,[=](){
        vSpin->setValue(valueSlider->sliderPosition());
    });
    connect(redSlider, &CustomSlider::sliderPressed,[=](){
        rSpin->setValue(redSlider->sliderPosition());
    });
    connect(greenSlider, &CustomSlider::sliderPressed,[=](){
        gSpin->setValue(greenSlider->sliderPosition());
    });
    connect(blueSlider,&CustomSlider::sliderPressed,[=](){
        bSpin->setValue(blueSlider->sliderPosition());
    });


    connect(hueSlider, &CustomSlider::sliderMoved,[=](){
        hSpin->setValue(hueSlider->sliderPosition());
    });
    connect(saturationSlider, &CustomSlider::sliderMoved,[=](){
        sSpin->setValue(saturationSlider->sliderPosition());
    });
    connect(valueSlider, &CustomSlider::sliderMoved,[=](){
        vSpin->setValue(valueSlider->sliderPosition());
    });
    connect(adjustSlider, &CustomSlider::sliderMoved,[=](){
        vSpin->setValue(valueSlider->sliderPosition());
    });
    connect(redSlider, &CustomSlider::sliderMoved,[=](){
        rSpin->setValue(redSlider->sliderPosition());
    });
    connect(greenSlider, &CustomSlider::sliderMoved,[=](){
        gSpin->setValue(greenSlider->sliderPosition());
    });
    connect(blueSlider,&CustomSlider::sliderMoved,[=](){
        bSpin->setValue(blueSlider->sliderPosition());
    });

    connect(alphaSlider,&CustomSlider::valueChanged,[=](){
        circlebg->setOpacity(alphaSlider->sliderPosition());
        setValueInColor();
        alphaSpin->setValue(alphaSlider->sliderPosition()/255.0f);
       // alphaSlider->setMaxLabel(QString::number(alphaSlider->sliderPosition()));
    });

    connect(alphaSpin,static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),this, [this](double val){
        int r = val *255.0f;
        alphaSlider->setValue(r);
    });

//connect(alphaSpin, QOverload<int>::of(&QDoubleSpinBox::valueChanged), this, [this](int newValue) { });


    connect(circlebg, SIGNAL(positionChanged(QColor)), this,SLOT(setSliders(QColor)));

    connect(rSpin, SIGNAL(valueChanged(int)),this, SLOT(changeBackgroundColorOfDisplayWidgetRgb()));
    connect(gSpin, SIGNAL(valueChanged(int)),this, SLOT(changeBackgroundColorOfDisplayWidgetRgb()));
    connect(bSpin, SIGNAL(valueChanged(int)),this, SLOT(changeBackgroundColorOfDisplayWidgetRgb()));
    connect(hSpin, SIGNAL(valueChanged(int)),this, SLOT(changeBackgroundColorOfDisplayWidgetHsv()));
    connect(sSpin, SIGNAL(valueChanged(int)),this, SLOT(changeBackgroundColorOfDisplayWidgetHsv()));
    connect(vSpin, SIGNAL(valueChanged(int)),this, SLOT(changeBackgroundColorOfDisplayWidgetHsv()));
    //        connect(redSlider, SIGNAL(valueChanged(int)),rSpin,SLOT(setValue(int)));
    //        connect(greenSlider, SIGNAL(valueChanged(int)),gSpin,SLOT(setValue(int)));
    //        connect(blueSlider, SIGNAL(valueChanged(int)),bSpin,SLOT(setValue(int)));
    //        connect(hueSlider, SIGNAL(valueChanged(int)),hSpin,SLOT(setValue(int)));
    //        connect(saturationSlider, SIGNAL(valueChanged(int)),sSpin,SLOT(setValue(int)));
    //        connect(valueSlider, SIGNAL(valueChanged(int)),vSpin,SLOT(setValue(int)));

    connect(hexEdit,SIGNAL(returnPressed()),this,SLOT(setColorFromHex()));

}

void ColorChooser::setColorBackground()
{
    colorDisplay->setGeometry(0,0,152,152);
    colorDisplay->setFixedSize(152,152);
    circlebg = new ColorCircle(colorDisplay, QColor(255,0,0));

}

void ColorChooser::setStyleForApplication()
{
    setStyle(new CustomStyle1(this->style()));
    setStyleSheet( " QSlider::handle { height: 0px; width:1px; margin: -2px 0px; background: rgba(250,100,100,0.9);}"
                   " QSlider::groove { border: 1px solid black; margin: 2px 0px;}"
                   " QSlider::add-page {background: rgba(90,90,90,1); border-radius: 3px;}"
                   " QSlider::sub-page {background: rgba(0,0,0,1); border-radius: 3px;}"
                   "QSlider::groove:vertical{background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,stop: 0 white, stop: 1 black); border-radius: 5px;}"
                   " QSlider::handle:vertical {height: 1px; width:1px; margin: -2px 0px; background: rgba(250,100,100,0.9); }"
                   " QSlider::add-page:vertical {background: rgba(0,0,0,0); border-radius: 3px;}"
                   " QSlider::sub-page:vertical {background: rgba(90,90,90,0); border-radius: 3px;}"
                   "QWidget{ background: rgba(50,50,50,1);border: 1px solid rgba(0,0,0,0); padding:0px; spacing : 0px;}"
                   "QLineEdit {border: 1px solid rgba(0,0,0,.4); color: rgba(255,255,255,.8); }"
                   "QPushButton{ background: rgba(60,60,60,1); color: rgba(255,255,255,.9); padding: 2px; border: 1px solid rgba(0,0,0,.4); border-radius:2px; }"
                   "QPushButton::checked {background: rgba(45,45,45,.9); }"
                   "QAbstractSpinBox { color:rgba(255,255,255,.8); }");

}

void ColorChooser::ErrorAdjustSliderValues()
{

}

void ColorChooser::exitPickerMode()
{
    picker->setChecked(false);
}




void ColorChooser::enterPickerMode()
{


    pixmap = QPixmap::grabWindow(QApplication::desktop()->winId());
      cbg->drawPixmap(pixmap);
}

void ColorChooser::pickerMode(bool ye)
{
    if(ye){
        enterPickerMode();
    }else{
        exitPickerMode();
    }
}

void ColorChooser::mouseMoveEvent(QMouseEvent *event)
{
//    if(picker->isChecked()){
//        rgb = pixmap.toImage().pixel(this->mapFromGlobal(QCursor::pos()));
//        color = color.fromRgb(rgb);
//        setSliders(color);
//        setValueInColor();
//        circlebg->drawSmallCircle(color);
//    }
}

void ColorChooser::mousePressEvent(QMouseEvent *event)
{
    if(picker->isChecked()){
        exitPickerMode();
    }

    QWidget::mousePressEvent(event);
}

void ColorChooser::enterEvent(QEvent *)
{

}

void ColorChooser::paintEvent(QPaintEvent *event)
{
    if(picker->isChecked()){
        QPainter painter(this) ;
        painter.drawPixmap(0,0,desktop->width(),desktop->height(),pixmap);
    }
}

void ColorChooser::setSliders(QColor color)
{
    blockSignals(true);
    setRgbSliders(color);
    setHsvSliders(color);
    blockSignals(false);
    setValueInColor();
    circlebg->drawSmallCircle(color);
}

void ColorChooser::setRgbSliders(QColor color)
{

    redSlider->setSliderPosition(color.red());
    greenSlider->setSliderPosition(color.green());
    blueSlider->setSliderPosition(color.blue());
    rSpin->blockSignals(true);
    rSpin->setValue(color.red());
    rSpin->blockSignals(false);
    gSpin->blockSignals(true);
    gSpin->setValue(color.green());
    gSpin->blockSignals(false);
    bSpin->blockSignals(true);
    bSpin->setValue(color.blue());
    bSpin->blockSignals(false);
    this->color = color;
    setSliderLabels();
}

void ColorChooser::setHsvSliders(QColor color)
{

    hueSlider->setSliderPosition(color.hsvHue());
    saturationSlider->setSliderPosition(color.hsvSaturation());
    valueSlider->setSliderPosition(color.value());
    hSpin->blockSignals(true);
    hSpin->setValue(color.hsvHue());
    hSpin->blockSignals(false);
    sSpin->blockSignals(true);
    sSpin->setValue(color.hsvSaturation());
    sSpin->blockSignals(false);
    vSpin->blockSignals(true);
    vSpin->setValue(color.value());
    vSpin->blockSignals(false);
    this->color = color;
    setSliderLabels();

}

void ColorChooser::setColorFromHex()
{
    color.setNamedColor(hexEdit->text());
    setSliders(color);
    setValueInColor();
    circlebg->drawSmallCircle(color);

}

void ColorChooser::setSliderLabels()
{
    circlebg->drawSmallCircle(color);
    hexEdit->setText(color.name());
	emit onColorChanged(color);

}


void ColorChooser::setValueInColor()
{


    circlebg->setValueInColor(color);
}



#include <QApplication>
#include <QEvent>
#include <QGraphicsEffect>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include "colorchooser.h"
#include "colorcircle.h"

#include <QDebug>

ColorChooser::ColorChooser(QWidget *parent) :
    QWidget()
{
    setWindowFlags(Qt::FramelessWindowHint |Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
    setAttribute(Qt::WA_TranslucentBackground);
	setWindowFlag(Qt::SubWindow);
	setAttribute(Qt::WA_QuitOnClose, false);

    setContentsMargins(20,20,20,20);
	setGeometry(0,0, 240, 410);

    gWidth = geometry().width();
    gHeight = geometry().height();
	cbg = new CustomBackground();
	cbg->hide();

    configureDisplay();
    setColorBackground();
    setConnections();
    setStyleForApplication();
    color = color.fromRgb(255,255,255);
    setSliders(color);
    alphaSpin->setValue(1.0);
	
}

ColorChooser::~ColorChooser()
{}

void ColorChooser::changeBackgroundColorOfDisplayWidgetHsv()
{
    color.setHsv(hSpin->value(),sSpin->value(), vSpin->value());
    setValueInColor();
    setRgbSliders(color);	
}
void ColorChooser::changeBackgroundColorOfDisplayWidgetRgb()
{
    color.setRgb(rSpin->value(),gSpin->value(), bSpin->value());
    setValueInColor();
    setHsvSliders(color);
}

void ColorChooser::configureDisplay()
{
	QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect;
	effect->setBlurRadius(10);
	effect->setXOffset(0);
	effect->setYOffset(1);
	effect->setColor(QColor(10,10,10));
	setGraphicsEffect(effect);

    groupBox = new QGroupBox();
    auto mainLayout = new QGridLayout();

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
    picker = new QPushButton("PICK");
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
	buttonLayout->setSpacing(0);
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
   
	connect(adjustSlider, SIGNAL(valueChanged(int)), valueSlider, SLOT(setValue(int)));
	connect(valueSlider, SIGNAL(valueChanged(int)), adjustSlider, SLOT(setValue(int)));


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

    redSlider->setMaximum(255);
    greenSlider->setMaximum(255);
    blueSlider->setMaximum(255);
    saturationSlider->setMaximum(255);
    valueSlider->setMaximum(255);
    adjustSlider->setRange(0,255);
    hueSlider->setMaximum(360);
    alphaSlider->setRange(0,255);
    adjustSlider->setSliderPosition(255);

    colorLayout->addSpacing(10);
    colorLayout->addWidget(colorDisplay);
    colorLayout->addWidget(adjustSlider);
    colorLayout->addSpacing(20);

	QLabel *r = new QLabel("R"); 
	QLabel *g = new QLabel("G");
	QLabel *b = new QLabel("B");
	QLabel *h = new QLabel("H");
	QLabel *s = new QLabel("S");
	QLabel *v = new QLabel("V");
	QLabel *a = new QLabel("A");	

	QFont f = r->font();
	f.setStyleStrategy(QFont::PreferQuality);

	r->setFont(f);
	b->setFont(f);
	h->setFont(f);
	s->setFont(f);
	v->setFont(f);
	a->setFont(f);
	rSliderSpin->addWidget(r);
    rSliderSpin->addWidget(redSlider);
    rSliderSpin->addWidget(rSpin);
	gSliderSpin->addWidget(g);
    gSliderSpin->addWidget(greenSlider);
    gSliderSpin->addWidget(gSpin);
	bSliderSpin->addWidget(b);
    bSliderSpin->addWidget(blueSlider);
    bSliderSpin->addWidget(bSpin);
	hSliderSpin->addWidget(h);
    hSliderSpin->addWidget(hueSlider);
    hSliderSpin->addWidget(hSpin);
	sSliderSpin->addWidget(s);
    sSliderSpin->addWidget(saturationSlider);
    sSliderSpin->addWidget(sSpin);
	vSliderSpin->addWidget(v);
    vSliderSpin->addWidget(valueSlider);
    vSliderSpin->addWidget(vSpin);

    rgbLayout->addLayout(rSliderSpin);
    rgbLayout->addLayout(gSliderSpin);
    rgbLayout->addLayout(bSliderSpin);
    hsvLayout->addLayout(hSliderSpin);
    hsvLayout->addLayout(sSliderSpin);
    hsvLayout->addLayout(vSliderSpin);

    stackHolder->addWidget(rgbHolder);
    stackHolder->addWidget(hsvHolder);
    stackHolder->addWidget(hexHolder);

    alphaSliderLayout->addSpacing(10);
	alphaSliderLayout->addWidget(a);
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
        if(picker->isChecked())exitPickerMode();
        stackHolder->setCurrentIndex(0);
    });
    connect(hsv, &QPushButton::pressed, [this](){
        if(picker->isChecked())exitPickerMode();
        stackHolder->setCurrentIndex(1);
    });
    connect(hex, &QPushButton::pressed, [this](){
        if(picker->isChecked())exitPickerMode();
        stackHolder->setCurrentIndex(2);
    });
    connect(cancel, &QPushButton::pressed, [this]() {
		if (picker->isChecked())exitPickerMode();
		emit onColorChanged(circlebg->initialColor);
		cbg->hide();
		hide();
    });
    connect(select, &QPushButton::clicked, [this]() {
		if (picker->isChecked())exitPickerMode();
		emit onColorChanged(circlebg->currentColor);
		cbg->hide();
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
    });

    connect(alphaSpin,static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),this, [this](double val){
        int r = val *255.0f;
        alphaSlider->setValue(r);
    });


    connect(circlebg, SIGNAL(positionChanged(QColor)), this,SLOT(setSliders(QColor)));

	connect(rSpin, static_cast<void (QSpinBox::*)(int) > (&QSpinBox::valueChanged), this, [this](int val) {

		blockSignals(true);
		redSlider->setValue(val);	
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetRgb();
		
	});
	connect(gSpin, static_cast<void (QSpinBox::*)(int) > (&QSpinBox::valueChanged), this, [this](int val) {

		blockSignals(true);
		greenSlider->setValue(val);	
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetRgb();
		
	});
	connect(bSpin, static_cast<void (QSpinBox::*)(int) > (&QSpinBox::valueChanged), this, [this](int val) {

		blockSignals(true);
		blueSlider->setValue(val);	
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetRgb();
	});
	connect(hSpin, static_cast<void (QSpinBox::*)(int) > (&QSpinBox::valueChanged), this, [this](int val) {

		blockSignals(true);
		hueSlider->setValue(val);
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetHsv();
	});
	connect(sSpin, static_cast<void (QSpinBox::*)(int) > (&QSpinBox::valueChanged), this, [this](int val) {
		blockSignals(true);
		saturationSlider->setValue(val);	
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetHsv();
	});
	connect(vSpin, static_cast<void (QSpinBox::*)(int) > (&QSpinBox::valueChanged), this, [this](int val) {

		blockSignals(true);
		valueSlider->setValue(val);
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetHsv();
	});
		
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
    setStyle(new SliderMoveToMouseClickPositionStyle(this->style()));
	setStyleSheet("QWidget#floater {background: #212121; }"
		"QSlider::sub-page {	border: 0px solid transparent;	height: 2px;	background: #3498db;	margin: 2px 0;}"
		"QSlider::groove:horizontal {    border: 0px solid transparent;    height: 4px;   background: #1e1e1e;   margin: 2px 0;}"
		"QSlider::handle:horizontal {    background-color: #CCC;    width: 12px;    border: 1px solid #1e1e1e;    margin: -5px 0px;   border-radius:7px;}"
		"QSlider::handle:horizontal:pressed {    background-color: #AAA;    width: 12px;   border: 1px solid #1e1e1e;    margin: -5px 0px;    border-radius: 7px;}"
		"QAbstractSpinBox { padding-right: 0px;	background: #292929; margin-right: -6px; }"
		"QAbstractSpinBox, QLabel { color:rgba(255,255,255,.8); } QAbstractSpinBox::down-button, QAbstractSpinBox::up-button { background: rgba(0,0,0,0); border : 1px solid rgba(0,0,0,0); }"
		"QWidget{ background: rgba(26,26,26,1);border: 1px solid rgba(0,0,0,0); padding:0px; spacing : 0px;}"
		"QSlider::handle:horizontal:disabled {    background-color: #bbbbbb;    width: 12px;    border: 0px solid transparent;    margin: -1px -1px;    border-radius: 4px;}"
		"QSlider::groove:vertical {background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,stop: 0 white, stop: 1 black); border-radius: 2px; width: 13px; }"
		" QSlider::handle:vertical {height: 3px; width:1px; margin: -2px 0px; background: rgba(50,148,213,0.9); }"
		" QSlider::add-page:vertical, QSlider::sub-page:vertical {background: rgba(0,0,0,0); border-radius: 1px;}"
		"QLineEdit {border: 1px solid rgba(0,0,0,.4); color: rgba(255,255,255,.8); }"
		"QPushButton{ background-color: #333; color: #DEDEDE; border : 0; padding: 4px 16px; }"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }"
		"QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }");
}


void ColorChooser::exitPickerMode()
{
    picker->setChecked(false);
	if(cbg->isExpanded)
	cbg->shrink();
}


void ColorChooser::enterPickerMode()
{	
	//qDebug() << windowModality();
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

void ColorChooser::showWithColor(QColor color, QMouseEvent* event, QString name)
{
	if (name == QString("outlineColor")) {
		picker->hide();
		setWindowModality(Qt::ApplicationModal);
	}
	qDebug() << name;
	
	circlebg->setInitialColor(color);
	cbg->show();
	int x;
	int y;
	int offset = 45;
	if (event->screenPos().x() + width() >= QApplication::desktop()->screenGeometry().width())	x = event->screenPos().x() - width()+ offset;
	else	x = event->screenPos().x()- offset;
	if (event->screenPos().y() + height() > QApplication::desktop()->screenGeometry().height())		y = event->screenPos().y() - height()+ offset;
	else	y = event->screenPos().y()- offset;

	setGeometry(x,y, width(), height());
	show();
}

void ColorChooser::mousePressEvent(QMouseEvent *event)
{
    if(picker->isChecked())     exitPickerMode();
    QWidget::mousePressEvent(event);
}

void ColorChooser::paintEvent(QPaintEvent *event)
{
    if(picker->isChecked()){
        QPainter painter(this) ;
        painter.drawPixmap(0,0,QApplication::desktop()->width(),QApplication::desktop()->height(),pixmap);
    }
}

void ColorChooser::leaveEvent(QEvent * event)
{
	if (!picker->isChecked()) {
		cbg->hide();
		hide();
	}
}

bool ColorChooser::eventFilter(QObject * obj, QEvent * event)
{
	switch (event->type())
	{
	case QEvent::KeyPress:
	case QEvent::KeyRelease:
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick:
	case QEvent::MouseMove:
		qDebug() << "moused moved";
	case QEvent::HoverEnter:
	case QEvent::HoverLeave:
	case QEvent::HoverMove:
	case QEvent::DragEnter:
	case QEvent::DragLeave:
	case QEvent::DragMove:
	case QEvent::Drop:
		return true;
	default:
		return QObject::eventFilter(obj, event);
	}
}

void ColorChooser::setSliders(QColor color)
{    
    setRgbSliders(color);
    setHsvSliders(color);    
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
	emit onColorChanged(color);
}
#include <QApplication>
#include <QEvent>
#include <QGraphicsEffect>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include "colorchooser.h"
#include "colorcircle.h"


ColorChooser::ColorChooser(QWidget *parent) :
    QWidget()
{
    setWindowFlags(Qt::FramelessWindowHint |Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
    setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setMouseTracking(true);
	setWindowFlag(Qt::SubWindow);
	setAttribute(Qt::WA_QuitOnClose, false);

    setContentsMargins(0,0,0,0);
	setGeometry(0,0, 255, 425);

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
	effect->setBlurRadius(15);
	effect->setXOffset(0);
	effect->setYOffset(1);
	effect->setColor(QColor(0,0,0,255));

    groupBox = new QGroupBox();
	groupBox->setGraphicsEffect(effect);
    auto mainLayout = new QGridLayout();

	auto opacityWidget = new QWidget;
	auto opacityWidgetLayout = new QVBoxLayout;
	opacityWidgetLayout->setContentsMargins(15, 15, 15, 15);
	opacityWidget->setLayout(opacityWidgetLayout);
	opacityWidget->setObjectName(QStringLiteral("opWidget"));	

	mainLayout->addWidget(opacityWidget);
	mainLayout->setSpacing(10);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	setLayout(mainLayout);
	opacityWidgetLayout->addWidget(groupBox);

    QVBoxLayout *vLayout = new QVBoxLayout();

	auto rgbLayout = new QGridLayout();
	auto hsvLayout = new QGridLayout();
    auto colorLayout = new QHBoxLayout();
    auto hexLayout = new QHBoxLayout();
    auto buttonLayout = new QHBoxLayout();
    auto finalButtonLayout = new QHBoxLayout();
    auto rSliderSpin = new QHBoxLayout();
    auto gSliderSpin = new QHBoxLayout();
    auto bSliderSpin = new QHBoxLayout();
    auto hSliderSpin = new QHBoxLayout();
    auto sSliderSpin = new QHBoxLayout();
    auto vSliderSpin = new QHBoxLayout();
    auto alphaSliderLayout = new QHBoxLayout();
    groupBox->setLayout(vLayout);

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

    redSlider = new QSlider(Qt::Horizontal);
    greenSlider = new QSlider(Qt::Horizontal);
    blueSlider = new QSlider(Qt::Horizontal);
    hueSlider = new QSlider(Qt::Horizontal);
    saturationSlider = new QSlider(Qt::Horizontal);
    valueSlider = new QSlider(Qt::Horizontal);
    alphaSlider = new QSlider(Qt::Horizontal);
    adjustSlider = new QSlider(Qt::Vertical);
   
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
	adjustSlider->setRange(0, 255);
	hueSlider->setMaximum(360);
	alphaSlider->setRange(0, 255);
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
	f.setWordSpacing(-1.5f);

	r->setFont(f);
	g->setFont(f);
	b->setFont(f);
	h->setFont(f);
	s->setFont(f);
	v->setFont(f);
	a->setFont(f);

	r->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	g->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	b->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	h->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	s->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	v->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	a->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

	rgbLayout->addWidget(r, 1, 1);
	rgbLayout->addWidget(redSlider, 1, 2);
	rgbLayout->addWidget(rSpin, 1, 3);
	rgbLayout->addWidget(g, 2, 1);
	rgbLayout->addWidget(greenSlider, 2, 2);
	rgbLayout->addWidget(gSpin, 2, 3);
	rgbLayout->addWidget(b, 3, 1);
	rgbLayout->addWidget(blueSlider, 3, 2);
	rgbLayout->addWidget(bSpin, 3, 3);

	hsvLayout->addWidget(h, 1, 1);
	hsvLayout->addWidget(hueSlider, 1, 2);
	hsvLayout->addWidget(hSpin, 1, 3);
	hsvLayout->addWidget(s, 2, 1);
	hsvLayout->addWidget(saturationSlider, 2, 2);
	hsvLayout->addWidget(sSpin, 2, 3);
	hsvLayout->addWidget(v, 3, 1);
	hsvLayout->addWidget(valueSlider, 3, 2);
	hsvLayout->addWidget(vSpin, 3, 3);

    stackHolder->addWidget(rgbHolder);
    stackHolder->addWidget(hsvHolder);
    stackHolder->addWidget(hexHolder);

    alphaSliderLayout->addSpacing(10);
	alphaSliderLayout->addWidget(a);
	alphaSliderLayout->addSpacing(7);
    alphaSliderLayout->addWidget(alphaSlider);
    alphaSliderLayout->addWidget(alphaSpin);
    alphaSliderLayout->addSpacing(5);
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
	connect(rgbBtn, &QPushButton::pressed, [this]() {
		if (picker->isChecked())exitPickerMode();
		stackHolder->setCurrentIndex(0);
	});
	connect(hsv, &QPushButton::pressed, [this]() {
		if (picker->isChecked())exitPickerMode();
		stackHolder->setCurrentIndex(1);
	});
	connect(hex, &QPushButton::pressed, [this]() {
		if (picker->isChecked())exitPickerMode();
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

	connect(picker, SIGNAL(toggled(bool)), this, SLOT(pickerMode(bool)));
	connect(cbg, SIGNAL(finished(bool)), this, SLOT(pickerMode(bool)));
	connect(cbg, SIGNAL(positionChanged(QColor)), this, SLOT(setSliders(QColor)));

    connect(hueSlider, &QSlider::sliderPressed, [=]() {
		hSpin->setValue(hueSlider->sliderPosition());
	});
    connect(saturationSlider, &QSlider::sliderPressed, [=]() {
		sSpin->setValue(saturationSlider->sliderPosition());
	});
    connect(valueSlider, &QSlider::sliderPressed, [=]() {
		vSpin->setValue(valueSlider->sliderPosition());
	});
    connect(adjustSlider, &QSlider::sliderPressed, [=]() {
		vSpin->setValue(valueSlider->sliderPosition());
	});
    connect(redSlider, &QSlider::sliderPressed, [=]() {
		rSpin->setValue(redSlider->sliderPosition());
	});
    connect(greenSlider, &QSlider::sliderPressed, [=]() {
		gSpin->setValue(greenSlider->sliderPosition());
	});
    connect(blueSlider, &QSlider::sliderPressed, [=]() {
		bSpin->setValue(blueSlider->sliderPosition());
	});


    connect(hueSlider, &QSlider::sliderMoved, [=]() {
		hSpin->setValue(hueSlider->sliderPosition());
	});
    connect(saturationSlider, &QSlider::sliderMoved, [=]() {
		sSpin->setValue(saturationSlider->sliderPosition());
	});
    connect(valueSlider, &QSlider::sliderMoved, [=]() {
		vSpin->setValue(valueSlider->sliderPosition());
	});
    connect(adjustSlider, &QSlider::sliderMoved, [=]() {
		vSpin->setValue(valueSlider->sliderPosition());
	});
    connect(redSlider, &QSlider::sliderMoved, [=]() {
		rSpin->setValue(redSlider->sliderPosition());
	});
    connect(greenSlider, &QSlider::sliderMoved, [=]() {
		gSpin->setValue(greenSlider->sliderPosition());
	});
    connect(blueSlider, &QSlider::sliderMoved, [=]() {
		bSpin->setValue(blueSlider->sliderPosition());
	});

    connect(alphaSlider, &QSlider::valueChanged, [=]() {
		circlebg->setOpacity(alphaSlider->sliderPosition());
		setValueInColor();
		alphaSpin->setValue(alphaSlider->sliderPosition() / 255.0f);
	});

	connect(alphaSpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double val) {
		int r = val * 255.0f;
		alphaSlider->setValue(r);
	});


	connect(circlebg, SIGNAL(positionChanged(QColor)), this, SLOT(setSliders(QColor)));
	connect(cbg, &CustomBackground::shouldHide, [=](bool b) {
		hide();
	});

	connect(rSpin, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged), this, [this](int val) {

		blockSignals(true);
		redSlider->setValue(val);
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetRgb();

	});
	connect(gSpin, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged), this, [this](int val) {

		blockSignals(true);
		greenSlider->setValue(val);
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetRgb();

	});
	connect(bSpin, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged), this, [this](int val) {

		blockSignals(true);
		blueSlider->setValue(val);
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetRgb();
	});
	connect(hSpin, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged), this, [this](int val) {

		blockSignals(true);
		hueSlider->setValue(val);
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetHsv();
	});
	connect(sSpin, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged), this, [this](int val) {
		blockSignals(true);
		saturationSlider->setValue(val);
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetHsv();
	});
	connect(vSpin, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged), this, [this](int val) {

		blockSignals(true);
		valueSlider->setValue(val);
		blockSignals(false);
		changeBackgroundColorOfDisplayWidgetHsv();
	});

	connect(hexEdit, SIGNAL(returnPressed()), this, SLOT(setColorFromHex()));
	connect(hexEdit, &QLineEdit::textChanged, [=]() {
		fromHexEdit = false;
		setColorFromHex();
		fromHexEdit = true;
	});
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
		"QAbstractSpinBox, QLabel { color:rgba(255,255,255,.8); padding: 3px } QAbstractSpinBox::down-button, QAbstractSpinBox::up-button { background: rgba(0,0,0,0); border : 1px solid rgba(0,0,0,0); }"
		"QWidget{ background: rgba(26,26,26,1);border: 1px solid rgba(0,0,0,0); padding:0px; spacing : 0px;}"
		"QSlider::handle:horizontal:disabled {    background-color: #bbbbbb;    width: 12px;    border: 0px solid transparent;    margin: -1px -1px;    border-radius: 4px;}"
		"QSlider::groove:vertical {background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,stop: 0 white, stop: 1 black); border-radius: 2px; width: 13px; }"
		" QSlider::handle:vertical {height: 3px; width:1px; margin: -2px 0px; background: rgba(50,148,213,0.9); }"
		" QSlider::add-page:vertical, QSlider::sub-page:vertical {background: rgba(0,0,0,0); border-radius: 1px;}"
		"QLineEdit {border: 1px solid rgba(0,0,0,.4); color: rgba(255,255,255,.8); }"
		"QPushButton{ background-color: #333; color: #DEDEDE; border : 0; padding: 4px 16px; }"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }"
		"QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }"
		"#opWidget{background:rgba(100,100,100,.02);}");
}

void ColorChooser::exitPickerMode()
{
    picker->setChecked(false);
	if(cbg->isExpanded)
	cbg->shrink();
}

void ColorChooser::enterPickerMode()
{	
	auto screen = QGuiApplication::primaryScreen();
	pixmap = screen->grabWindow(QApplication::desktop()->winId());
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
	else cbg->show();
	
	
	circlebg->setInitialColor(color);
	
	int x;
	int y;
	int offset = -15;
	if (event->screenPos().x() + width()+offset >= QApplication::desktop()->screenGeometry().width())	x = event->screenPos().x() - width()- offset;
	else	x = event->screenPos().x()+ offset;
	if (event->screenPos().y() + height()+offset > QApplication::desktop()->screenGeometry().height())	y = event->screenPos().y() - height()-offset;
	else	y = event->screenPos().y()+ offset;

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
    circlebg->drawSmallCircle(color);
}

void ColorChooser::setSliderLabels()
{
    circlebg->drawSmallCircle(color);
    if(fromHexEdit)hexEdit->setText(color.name());
	emit onColorChanged(color);
}

void ColorChooser::setValueInColor()
{
    circlebg->setValueInColor(color);
	emit onColorChanged(color);
}

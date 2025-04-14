/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "colorview.h"

#include <QGuiApplication>
#include <QScreen>
#include <QRect>
#include <QCursor>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <QtMath>
#include <QDebug>
#include <QPainter>
#include <QGraphicsEffect>
#include <QMouseEvent>
#include <QWindow>

ColorView* ColorView::instance = 0;


ColorView::ColorView(QColor color, QWidget *parent ) : QWidget(parent)
{
    this->initialColor = color;
    configureView();
    configureConnections();
    inputCircle->setInitialColor(color);
	layout()->update();
	layout()->activate();
}

void ColorView::configureView()
{
	setWindowModality(Qt::ApplicationModal);

    setAttribute(Qt::WA_MacShowFocusRect, false);
    setStyleSheet(StyleSheet::QWidgetDark());
	setWindowFlag(Qt::SubWindow);
	setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint );
	setAttribute(Qt::WA_TranslucentBackground);

	//holder will be the invisible widget that houses everything
	auto holder = new QWidget;
	auto holderLayout = new QVBoxLayout;

	auto effect = new QGraphicsDropShadowEffect;
	effect->setOffset(0);
	effect->setBlurRadius(15);
	effect->setColor(QColor(0,0,0));
	holder->setGraphicsEffect(effect);


	holderLayout->addWidget(holder);
	setLayout(holderLayout);
	holderLayout->setContentsMargins(40, 40, 40, 40);

    auto layout = new QVBoxLayout;
    holder->setLayout(layout);


    auto hlayoutWidget = new QGridLayout;
    auto hlayoutFormat = new QGridLayout;
    auto hlayoutSliders = new QGridLayout;
    auto hsvlayoutSliders = new QGridLayout;
    auto aLayout = new QGridLayout;
    auto hlayoutButton = new QGridLayout;

    auto hWidget = new  QWidget;
    auto hFormat = new  QWidget;
    auto hSliders = new QWidget;
    auto hsvSliders = new QWidget;
    auto hButton = new  QWidget;
    auto aWidget = new  QWidget;

    hWidget->setLayout(hlayoutWidget);
    hFormat->setLayout(hlayoutFormat);
    hSliders->setLayout(hlayoutSliders);
    hsvSliders->setLayout(hsvlayoutSliders);
    hButton->setLayout(hlayoutButton);
    aWidget->setLayout(aLayout);

    stackWidget = new QStackedWidget(this);
	setStyle(new SliderMoveToMouseClickPositionStyle(this->style()));

    rSlider = new QSlider(Qt::Horizontal, this);
    gSlider = new QSlider(Qt::Horizontal, this);
    bSlider = new QSlider(Qt::Horizontal, this);
    hSlider = new QSlider(Qt::Horizontal, this);
    sSlider = new QSlider(Qt::Horizontal, this);
    vSlider = new QSlider(Qt::Horizontal, this);
    aSlider = new QSlider(Qt::Horizontal, this);
    
    rSlider->setStyleSheet(StyleSheet::QSlider());
    gSlider->setStyleSheet(StyleSheet::QSlider());
    bSlider->setStyleSheet(StyleSheet::QSlider());
    hSlider->setStyleSheet(StyleSheet::QSlider());
    sSlider->setStyleSheet(StyleSheet::QSlider());
    vSlider->setStyleSheet(StyleSheet::QSlider());
    aSlider->setStyleSheet(StyleSheet::QSlider());

    rBox = new QSpinBox(this);
    gBox = new QSpinBox(this);
    bBox = new QSpinBox(this);
    hBox = new QSpinBox(this);
    sBox = new QSpinBox(this);
    vBox = new QSpinBox(this);
    aBox = new QSpinBox(this);
    
    rBox->setStyleSheet(StyleSheet::QSpinBox());
    gBox->setStyleSheet(StyleSheet::QSpinBox());
    bBox->setStyleSheet(StyleSheet::QSpinBox());
    hBox->setStyleSheet(StyleSheet::QSpinBox());
    sBox->setStyleSheet(StyleSheet::QSpinBox());
    vBox->setStyleSheet(StyleSheet::QSpinBox());
    aBox->setStyleSheet(StyleSheet::QSpinBox());

    auto rLabel = new QLabel("R");
    auto gLabel = new QLabel("G");
    auto bLabel = new QLabel("B");
    auto hLabel = new QLabel("H");
    auto sLabel = new QLabel("S");
    auto vLabel = new QLabel("V");
    auto aLabel = new QLabel("A");
    auto format = new QLabel("Format:");
    format->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    rLabel->setStyleSheet(StyleSheet::QLabelWhite());
    gLabel->setStyleSheet(StyleSheet::QLabelWhite());
    bLabel->setStyleSheet(StyleSheet::QLabelWhite());
    hLabel->setStyleSheet(StyleSheet::QLabelWhite());
    sLabel->setStyleSheet(StyleSheet::QLabelWhite());
    vLabel->setStyleSheet(StyleSheet::QLabelWhite());
    aLabel->setStyleSheet(StyleSheet::QLabelWhite());
    format->setStyleSheet(StyleSheet::QLabelWhite());

    lineEditor = new QLineEdit(this);
    lineEditor->setEnabled(false);
    
    rgb = new QPushButton("RGB", this);
    hsv = new QPushButton("HSV", this);
    hex = new QPushButton("HEX", this);
    rgb->setCheckable(true);
    hsv->setCheckable(true);
    hex->setCheckable(true);
    grp.addButton(rgb);
    grp.addButton(hsv);
    grp.addButton(hex);
    grp.setExclusive(true);
    rgb->setChecked(true);
    
    rgb->setStyleSheet(StyleSheet::QPushButtonGrouped());
    hsv->setStyleSheet(StyleSheet::QPushButtonGrouped());
    hex->setStyleSheet(StyleSheet::QPushButtonGrouped());

    auto buttonWidget = new QWidget;
    auto buttonWidgetLayout = new QHBoxLayout;
    buttonWidget->setLayout(buttonWidgetLayout);
    buttonWidgetLayout->addWidget(format);
    buttonWidgetLayout->addStretch();
    buttonWidgetLayout->addWidget(rgb);
    buttonWidgetLayout->addWidget(hsv);
    buttonWidgetLayout->addWidget(hex);
    buttonWidgetLayout->setSpacing(0);
    buttonWidgetLayout->setContentsMargins(0, 10, 0, 10);

    lineEditor->setStyleSheet(StyleSheet::QLineEdit());
    confirm = new QPushButton("confirm", this);
    reset = new QPushButton("reset", this);
    cancel = new QPushButton("cancel", this);
    confirm->setStyleSheet(StyleSheet::QPushButtonGreyscale());
    reset->setStyleSheet(StyleSheet::QPushButtonGreyscale());
    cancel->setStyleSheet(StyleSheet::QPushButtonGreyscale());

    display = new ColorDisplay;
    circle = new ColorCircle();
    inputCircle = new InputCircle(circle);
    inputCircle->setStyleSheet("QWidget{background: rgba(20,20,20,1);}");
    inputCircle->move(inputCircle->offset,inputCircle->offset);
    display->initialColor = this->initialColor;

	setMaximumWidth(circle->squareSize);

    layout->addWidget(hWidget, 0);
    layout->addWidget(hFormat, 1);
    layout->addWidget(stackWidget, 2);
    layout->addWidget(aWidget, 3);
    layout->addWidget(hButton, 4);
    layout->setSpacing(0);


    
    aLayout->addWidget(aLabel,3,0, 1, 1);
    aLayout->addWidget(aSlider,3,1, 1, 1);
    aLayout->addWidget(aBox,3,2, 1, 1);
    aLayout->setContentsMargins(7, 0, 7, 0);
    
    hlayoutWidget->addWidget(circle, 0, 0);
    hlayoutWidget->addWidget(display, 2, 0);

    hlayoutFormat->addWidget(buttonWidget, 0, 0);
    hlayoutFormat->addWidget(lineEditor, 1, 0);
    hlayoutFormat->setContentsMargins(10, 10, 5, 10);

    hlayoutSliders->addWidget(rLabel, 0, 0);
    hlayoutSliders->addWidget(gLabel, 1, 0);
    hlayoutSliders->addWidget(bLabel, 2, 0);
    hsvlayoutSliders->addWidget(hLabel,0,0);
    hsvlayoutSliders->addWidget(sLabel,1,0);
    hsvlayoutSliders->addWidget(vLabel,2,0);

    hlayoutSliders->addWidget(rSlider, 0, 1);
    hlayoutSliders->addWidget(gSlider, 1, 1);
    hlayoutSliders->addWidget(bSlider, 2, 1);
    hsvlayoutSliders->addWidget(hSlider,0,1);
    hsvlayoutSliders->addWidget(sSlider,1,1);
    hsvlayoutSliders->addWidget(vSlider,2,1);

    hlayoutSliders->addWidget(rBox, 0, 2);
    hlayoutSliders->addWidget(gBox, 1, 2);
    hlayoutSliders->addWidget(bBox, 2, 2);
    hsvlayoutSliders->addWidget(hBox,0,2);
    hsvlayoutSliders->addWidget(sBox,1,2);
    hsvlayoutSliders->addWidget(vBox,2,2);
    
    stackWidget->addWidget(hSliders);
    stackWidget->addWidget(hsvSliders);
    stackWidget->setContentsMargins(0,0,0,0);

    hlayoutButton->setSpacing(3);
    hlayoutButton->addWidget(confirm, 0, 0);
    hlayoutButton->addWidget(reset, 0, 1 );
    hlayoutButton->addWidget(cancel, 0, 2 );
    hlayoutButton->setContentsMargins(10, 20, 5, 0);
}

void ColorView::configureStylesheet() {
    setStyleSheet(
            "background: rgba(25,25,25,1);;"
            );
}

void ColorView::configureConnections()
{

    connect(display, &ColorDisplay::pick,[=](){
        createOverlay();
    });
    
    
    rBox->setRange(0, 255);
    bBox->setRange(0, 255);
    gBox->setRange(0, 255);
    hBox->setRange(0, 359);
    sBox->setRange(0, 100);
    vBox->setRange(0, 100);
    aBox->setRange(0, 255);
    rSlider->setRange(rBox->minimum(),rBox->maximum());
    gSlider->setRange(gBox->minimum(),gBox->maximum());
    bSlider->setRange(bBox->minimum(),bBox->maximum());
    hSlider->setRange(hBox->minimum(),hBox->maximum());
    sSlider->setRange(sBox->minimum(),sBox->maximum());
    vSlider->setRange(vBox->minimum(),vBox->maximum());
    aSlider->setRange(aBox->minimum(),aBox->maximum());
    aSlider->setValue(255);
    
    
    
    connect(rSlider, &QSlider::valueChanged,[=](int value){
        sliderUpdatesSpinboxWithoutSignal(rBox, value);
        inputCircle->setRed(value);
    });
    connect(gSlider, &QSlider::valueChanged,[=](int value){
        sliderUpdatesSpinboxWithoutSignal(gBox, value);
        inputCircle->setGreen(value);
        
    });
    connect(bSlider, &QSlider::valueChanged,[=](int value){
        sliderUpdatesSpinboxWithoutSignal(bBox, value);
        inputCircle->setBlue(value);
    });
    connect(hSlider, &QSlider::valueChanged,[=](int value){
        sliderUpdatesSpinboxWithoutSignal(hBox, value);
        inputCircle->setHue(value);
    });
    connect(sSlider, &QSlider::valueChanged,[=](int value){
        sliderUpdatesSpinboxWithoutSignal(sBox, value);
        inputCircle->setSaturation(value*factor);
        
    });
    connect(vSlider, &QSlider::valueChanged,[=](int value){
        sliderUpdatesSpinboxWithoutSignal(vBox, value);
        inputCircle->setValue(value*factor);
    });
   
    connect(aSlider, &QSlider::valueChanged,[=](int value){
        sliderUpdatesSpinboxWithoutSignal(aBox, value);
        inputCircle->setAlpha(value);
    });
    
    
    connect(rBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),[=](int value){
        spinboxUpdatesSliderWithoutSignal(rSlider, value);
    });
    connect(gBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),[=](int value){
        spinboxUpdatesSliderWithoutSignal(gSlider, value);
    });
    connect(bBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),[=](int value){
        spinboxUpdatesSliderWithoutSignal(bSlider, value);
    });
    connect(hBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),[=](int value){
        spinboxUpdatesSliderWithoutSignal(hSlider, value);
    });
    connect(sBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),[=](int value){
        spinboxUpdatesSliderWithoutSignal(sSlider, value);
    });
    connect(vBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),[=](int value){
        spinboxUpdatesSliderWithoutSignal(vSlider, value);
    });
    connect(aBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),[=](int value){
        spinboxUpdatesSliderWithoutSignal(aSlider, value);
    });
    
    connect(inputCircle,&InputCircle::positionChanged,[=](QColor col){
        this->colorCircleUpdatesSliderWithoutNotification(rSlider, rBox, col);
        this->colorCircleUpdatesSliderWithoutNotification(gSlider, gBox, col);
        this->colorCircleUpdatesSliderWithoutNotification(bSlider, bBox, col);
        this->colorCircleUpdatesSliderWithoutNotification(hSlider, hBox, col);
        this->colorCircleUpdatesSliderWithoutNotification(sSlider, sBox, col);
        this->colorCircleUpdatesSliderWithoutNotification(vSlider, vBox, col);
        this->colorCircleUpdatesSliderWithoutNotification(aSlider, aBox, col);
		emit onColorChanged(col);
    });

    connect(inputCircle, &InputCircle::updateColorText,[=](QColor col){
        display->color = col;
        display->setToolTip(col.name());
        display->repaint();
        this->lineEditor->setText(colorNameFromSpac(col));
    });
    
    connect(inputCircle, &InputCircle::selectingColor,[=](bool val){
        picking = val;
    });
    
    connect(inputCircle, &InputCircle::updateValue,[=](int value){
        vSlider->blockSignals(true);
        vBox->blockSignals(true);
        
        vSlider->setValue(value/factor);
        vBox->setValue(value/factor);

        vSlider->blockSignals(false);
        vBox->blockSignals(false);

    });

	connect(inputCircle, &InputCircle::colorChanged, [=](QColor col) {
		emit onColorChanged(col);
	});
    
    connect(confirm, &QPushButton::clicked, [=](){
        hide();
		emit exiting();
    });
    connect(reset, &QPushButton::clicked, [=](){
        inputCircle->setColor(inputCircle->getInitialColor());
    });
    connect(cancel, &QPushButton::clicked, [=](){
		inputCircle->setColor(inputCircle->getInitialColor());
        hide();
		emit exiting();
    });
    
    connect(rgb, &QPushButton::clicked, [=](){
        stackWidget->setCurrentIndex(0);
        spec = QColor::Rgb;
        this->lineEditor->setText(colorNameFromSpac(inputCircle->color));
        lineEditor->setEnabled(false);
    });
    connect(hsv, &QPushButton::clicked, [=](){
        stackWidget->setCurrentIndex(1);
        spec = QColor::Hsv;
        this->lineEditor->setText(colorNameFromSpac(inputCircle->color));
        lineEditor->setEnabled(false);

    });
    connect(hex, &QPushButton::clicked, [=](){
        stackWidget->setCurrentIndex(0);
        spec = QColor::Cmyk; // subs for hex
        this->lineEditor->setText(colorNameFromSpac(inputCircle->color));
        lineEditor->setEnabled(true);

    });
    
    connect(lineEditor, &QLineEdit::textChanged, [=]() {
        setColorFromHex();
    });
    connect(lineEditor, &QLineEdit::returnPressed, [=]() {
        setColorFromHex();
    });
}

void ColorView::spinboxUpdatesSliderWithoutSignal(QSlider *slider, int value) {
    slider->blockSignals(true);
    slider->setValue(value);
    if(slider == rSlider)     inputCircle->setRed(value);
    if(slider == gSlider)     inputCircle->setGreen(value);
    if(slider == bSlider)     inputCircle->setBlue(value);
    if(slider == hSlider)     inputCircle->setHue(value);
    if(slider == sSlider)     inputCircle->setSaturation(value*factor);
    if(slider == aSlider)     inputCircle->setAlpha(value);
    if(slider == vSlider)     inputCircle->setValue(value*factor);
    slider->blockSignals(false);
}

void ColorView::colorCircleUpdatesSliderWithoutNotification(QSlider *slider, QSpinBox *box, QColor &color) {
    slider->blockSignals(true);
    box->blockSignals(true);
    
    if(slider == rSlider)   slider->setValue(color.red());
    if(slider == gSlider)   slider->setValue(color.green());
    if(slider == bSlider)   slider->setValue(color.blue());
    if(slider == hSlider)   slider->setValue(color.hue());
    if(slider == sSlider)   slider->setValue(color.saturation()/factor);
    if(slider == vSlider)   slider->setValue(color.value()/factor);
    if(slider == aSlider)   slider->setValue(color.alpha());
    if(box == rBox)         box->setValue(color.red());
    if(box == gBox)         box->setValue(color.green());
    if(box == bBox)         box->setValue(color.blue());
    if(box == hBox)         box->setValue(color.hue());
    if(box == sBox)         box->setValue(color.saturation()/factor);
    if(box == vBox)         box->setValue(color.value()/factor);
    if(box == aBox)         box->setValue(color.alpha());
    
    slider->blockSignals(false);
    box->blockSignals(false);

}

void ColorView::showAtPosition(QMouseEvent *event, QColor color)
{
	initialColor = color;
	inputCircle->setInitialColor(color);

    QScreen* qscreen = QGuiApplication::screenAt(QCursor::pos());
    int mouseScreen = QGuiApplication::screens().indexOf(qscreen);
	auto screen = QGuiApplication::screens().at(mouseScreen);
	auto geom = screen->geometry();
	
	int x;
	int y;
	int offset = -15;
	int factor = 3;
	int screenX = event->screenPos().x();
	int screenY = event->screenPos().y();
	y = screenY - 40;
    if (screenX + width() + offset >= screen->geometry().width())	x = screenX - width() - offset* factor;
	else	x = (screenX + offset * factor);
	if (screenY + height() + offset > geom.height())	y = screenY - height() - offset* factor;
	if (geometry().y() < 0) y = 10;

	setGeometry(x, y, width(), height());
	setMaximumHeight(height());
	show();

	if (geometry().y() < 0) move(geometry().x(), 10); // if too high
}

ColorView * ColorView::getSingleston()
{
	if (instance == nullptr)
		instance = new ColorView();

	return instance;
}

QString ColorView::colorNameFromSpac(QColor col) {
    QString name;
    switch (spec)
    {
        case QColor::Rgb:
            name = QString("rgb(%1, %2, %3)").arg(col.red()).arg(col.green()).arg(col.blue());
            break;
        case QColor::Hsv:
            name = QString("hsv(%1, %2%, %3%)").arg(col.hue()).arg((int)(col.saturation()/2.55)).arg((int)(col.value()/2.55));
            break;
        case QColor::Hsl:
            break;
        case QColor::Cmyk:
            name = col.name();
            break;
        default:
            break;
    }
    return name;
}

void ColorView::sliderUpdatesSpinboxWithoutSignal(QSpinBox *box, int value) {
    box->blockSignals(true);
    box->setValue(value);
    box->blockSignals(false);
}

void ColorView::createOverlay() {
    picking = true;
    int num = QGuiApplication::screens().count();
    list = new QList<Overlay*>;
    
    QList<QScreen*> screens = QGuiApplication::screens();
    for (int i =0 ; i< num; i++){
        auto geo =  screens.at(i)->geometry();
        auto overlay = new Overlay(geo, i);
        list->append(overlay);
        
        connect(overlay, &Overlay::closeWindow,[=](){
            for( auto win : *list){
                win->close();
                win->deleteLater();
            }
			delete list;
			list = nullptr;
            show();
            QCursor::setPos(inputCircle->getGlobalPositionFromIndicatorCircle());
            picking = false;
        });
        
        connect(overlay, &Overlay::color,[=](QColor col){
            inputCircle->setColor(col);
        });
    }
}

void ColorView::leaveEvent(QEvent *) {
	if (!picking) {
		this->hide();
		emit exiting();
	}
}

void ColorView::enterEvent(QEvent *)
{
	picking = false;
}

void ColorView::mousePressEvent(QMouseEvent *e) {
    isPressed = true;
    oldPos = e->globalPos();
}

void ColorView::mouseMoveEvent(QMouseEvent *e) {
    QPoint delta = e->globalPos() - oldPos;
    if (isPressed) move(x() + delta.x(), y() + delta.y());
    oldPos = e->globalPos();
}

void ColorView::mouseReleaseEvent(QMouseEvent *) {
    isPressed = false;
}

void ColorView::setColorFromHex() {
    QColor col;
    QString name = lineEditor->text();
    if(name.length() < 5) return;
    if(QColor::isValidColor(name)) col.setNamedColor(name);
    if(col.isValid())inputCircle->setColor(col);
}












ColorCircle::ColorCircle(QWidget *parent) : QWidget(parent)
{
    squareSize = 200;
    radius = squareSize / 2 ;
    centerPoint.setX(radius );
    centerPoint.setY(radius );
    colorValue = 255;
    offset = 5;
    padding = offset*2;
    
    auto redefinedSize = squareSize + padding;
    setMinimumSize({redefinedSize,redefinedSize});

    drawImage();
}

QImage *ColorCircle::drawImage()
{
    if(!image)
        image = new QImage(squareSize, squareSize, QImage::Format_ARGB32_Premultiplied);

    //draw color circle image
        for (int i = 0; i < image->width(); i++) {
            for (int j = 0; j < image->height(); j++) {

                QPoint point(i, j);
                int d = qPow(point.rx() - centerPoint.rx(), 2) + qPow(point.ry() - centerPoint.ry(), 2);
                if (d <= qPow(radius, 2)) {

                    saturation = (qSqrt(d) / radius)*255.0f;
                    qreal theta = qAtan2(point.ry() - centerPoint.ry(), point.rx() - centerPoint.rx());

                    theta = (180 + 90 + static_cast<int>(qRadiansToDegrees(theta))) % 360;
                    color.setHsv(theta, saturation, colorValue, alpha);
                    image->setPixelColor(i, j, color);
                }
                else {
                    color.setRgb(126, 26, 26,0);
                    image->setPixelColor(i, j, color);
                }
            }
        }
    
        return image;
}

void ColorCircle::paintEvent(QPaintEvent *event) {

    QWidget::paintEvent( event);

    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing);

    painter.drawImage(offset , offset, *image);

    painter.setPen(QPen(QColor(250,250,250),2));
    painter.drawEllipse( offset, offset, image->width(), image->height());
}

void ColorCircle::setAlpha(int alpha) {
    this->alpha = alpha;
    drawImage();
    repaint();
}

void ColorCircle::setColor(QColor col) {
    
}




InputCircle::InputCircle(ColorCircle *parent) : QWidget(parent) {
    
    squareSize = parent->squareSize;
    radius = parent->radius;
    centerPoint = parent->centerPoint;
    offset = parent->offset;
    color = parent->color;
    if(parent) this->parent = parent;

    setMinimumSize({squareSize,squareSize});
}

void InputCircle::mousePressEvent(QMouseEvent *event) {
    drawSmallDot = false;
    
    QVector2D mousePos(event->pos().x(), event->pos().y());
    auto centerPos = QVector2D(centerPoint.x(), centerPoint.y());
    auto diff = mousePos - centerPos;
    if (diff.length() > radius)  return;
    
    pressed = true;
    setCirclePosition(event);
    emit selectingColor(true);
    repaint();
}

void InputCircle::mouseMoveEvent(QMouseEvent *event) {
    if(pressed) setCirclePosition(event);
}

void InputCircle::setCirclePosition(QMouseEvent *event) {
    QVector2D mousePos(event->pos().x(), event->pos().y());
    auto centerPos = QVector2D(centerPoint.x(), centerPoint.y());
    auto diff = mousePos - centerPos;
    if (diff.length() > radius)     diff = diff.normalized() * radius;
    auto position = centerPos + diff;
    pos.setX(position.x());
    pos.setY(position.y());

    color = getCurrentColorFromPosition();
    emit positionChanged(color);
    emit updateColorText(color);
}

QColor InputCircle::getCurrentColorFromPosition() {
    int d = qPow(pos.rx() - centerPoint.rx(), 2) + qPow(pos.ry() - centerPoint.ry(), 2);
    auto saturation = (qSqrt(d) / radius)*255.0f;
    qreal theta = qAtan2(pos.ry() - centerPoint.ry(), pos.rx() - centerPoint.rx());
    theta = (180 + 90 + (int)qRadiansToDegrees(theta)) % 360;
    if (saturation >= 253)    saturation = 255;
    if (saturation <= 2)    saturation = 0;
    if (colorValue >= 253)    colorValue = 255;
    if (colorValue <= 2)    colorValue = 0;
    
    color.setHsv(theta, saturation, colorValue, this->alpha);
    return color;
}

void InputCircle::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    
    if(drawSmallDot){
        painter.setPen(QPen(QColor(20,20,20),2));
        painter.drawEllipse(QPoint(pos.x(), pos.y()), 2, 2);
    }
}

void InputCircle::drawIndicatorCircle(QColor color) {
    drawSmallDot = true;
    
    qreal theta = qDegreesToRadians((qreal)color.hsvHue() + 90);
    auto sat = color.hsvSaturation() / 255.0f * radius;
    qreal x = sat * qCos(theta) + centerPoint.x();
    qreal y = sat * qSin(theta) + centerPoint.y();
    pos = QPoint(x, y);
    globalPos = mapToGlobal(pos);
    emit colorChanged(color);
    emit updateColorText(color);
    repaint();
}

void InputCircle::setRed(int red) {
    color.setRed(red);
    checkColorValue();
    drawIndicatorCircle(color);
}

void InputCircle::setGreen(int green) {
    color.setGreen(green);
    checkColorValue();
    drawIndicatorCircle(color);
}

void InputCircle::setBlue(int blue) {
    color.setBlue(blue);
    checkColorValue();
    drawIndicatorCircle(color);
}

void InputCircle::setAlpha(int alpha) {
    this->alpha = alpha;
    color.setAlpha(alpha);
    parent->setAlpha(alpha);
    checkColorValue();
    drawIndicatorCircle(color);
}

void InputCircle::mouseReleaseEvent(QMouseEvent *) {
    drawSmallDot = true;
    pressed = false;
    emit selectingColor(false);
    this->repaint();
}

void InputCircle::setHue(int hue) {
    color.setHsv(hue, color.saturation(), color.value(), alpha);
    checkColorValue();
    drawIndicatorCircle(color);
}

void InputCircle::setSaturation(int saturation) {
    if(saturation > 253.9) saturation = 255;
    color.setHsv(color.hue(), saturation, color.value(), alpha);
    checkColorValue();
    drawIndicatorCircle(color);
}

void InputCircle::setValue(int value) {
    if(value > 253.9) value = 255;
    color.setHsv(color.hue(), color.saturation(), value, alpha);
    checkColorValue(false);
    drawIndicatorCircle(color);
}

void InputCircle::setInitialColor(QColor col) {
    this->color = col;
    this->initialColor = col;
    drawIndicatorCircle(col);
    checkColorValue();
    emit positionChanged(col);
}

bool InputCircle::checkColorValue(bool val) {
    if(colorValue != color.value()){
        colorValue = color.value();
        parent->colorValue = color.value();
        parent->drawImage();
        if(val) emit updateValue(color.value());
        return true;
    }
    return false;
}

void InputCircle::setColor(QColor color) {
    this->color = color;
    drawIndicatorCircle(color);
    checkColorValue();
    parent->repaint();
    emit positionChanged(color);
}

QColor InputCircle::getInitialColor() {
    return initialColor;
}

QPoint InputCircle::getGlobalPositionFromIndicatorCircle() {
    return globalPos;
}









ColorDisplay::ColorDisplay(QWidget *parent) : QWidget(parent) {
    int heightForDisplay = 50/2;
    setMinimumHeight(heightForDisplay);
    auto layout = new QHBoxLayout;
    setLayout(layout);
    
    auto button = new QPushButton(this);
    button->setIcon(QIcon((":/images/dropper.png")));
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    button->setIconSize({heightForDisplay/2,heightForDisplay/2});
    
    layout->addStretch();
    layout->addWidget(button);
    layout->addSpacing(16);
    layout->setContentsMargins(0, 0, 0, 0);
    
    connect(button, &QPushButton::clicked,[=](){
        emit pick();
    });
   
    button->setStyleSheet(StyleSheet::QPushButtonInvisible());
}

void ColorDisplay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(color);
    //draw entire display
    painter.drawRoundedRect(QRect(0,0,width(),height()), 25/2, 25/2);
}



Overlay::Overlay(QRect sg, int screenNumber, QWidget *parent) : QWidget(parent) {
    setWindowFlags( windowFlags() | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint | Qt::FramelessWindowHint);

    setMouseTracking(true);
    setAttribute(Qt::WA_QuitOnClose, false);

    qreal overlayOpacity = 0.01f;
    setWindowOpacity(overlayOpacity);
    
    move(sg.x(), sg.y());
    resize(sg.width(), sg.height());
	screenNum = screenNumber;
	auto screen = QGuiApplication::screens().at(screenNum);
	auto geom = screen->geometry();
    if (screen) {
        pixmap = screen->grabWindow(0, geom.x(), geom.y(), geom.width(), geom.height());
    }
	show();
	repaint();
}

void Overlay::mousePressEvent(QMouseEvent *event) {
    QWidget::mousePressEvent(event);
    emit closeWindow();
}

void Overlay::mouseMoveEvent(QMouseEvent *eve) {

	auto screen = QGuiApplication::screens().at(screenNum);
	auto geom = screen->geometry();
    if (screen) {
        pixmap = screen->grabWindow(0, geom.x(), geom.y(), geom.width(), geom.height());
    }

	auto colo = pixmap.toImage().pixelColor(mapFromGlobal(QCursor::pos()));
    emit color(colo);
    QWidget::mouseMoveEvent(eve);
}

void Overlay::mouseReleaseEvent(QMouseEvent *) {
//    this->close();
}


void Overlay::paintEvent(QPaintEvent *e) {
	QPainter painter(this);
	painter.drawPixmap(0, 0, width(), height(), pixmap);
}





ValueSlider::ValueSlider(QWidget *parent) : QSlider(parent) {
    setOrientation(Qt::Horizontal);
    setStyleSheet(
                  QString(
    "QSlider::groove:horizontal {background: qlineargradient(x1: 1, y1: 0, x2: 0, y2: 0,stop: 0 white, stop: 1 black); border-radius: 1px; border: 1px solid rgba(0,0,0,1); }"
    " QSlider::handle:horizontal {height: 3px; width:14px; margin: -2px 0px; background: rgba(50,148,213,1); }"
    " QSlider::add-page:horizontal, QSlider::sub-page:horizontal {background: rgba(0,0,0,0); border-radius: 1px;}"

    ));
    
}

void ValueSlider::setColor(QColor col) {
    this->col = col;
}




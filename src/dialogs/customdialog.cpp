/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "customdialog.h"
#include "../misc/stylesheet.h"

#include <QGraphicsDropShadowEffect>
#include <QVBoxLayout>

CustomDialog::CustomDialog(Qt::Orientation ori) : QDialog()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint );
    setAttribute(Qt::WA_TranslucentBackground);


	mainLayout = new QVBoxLayout();
    setLayout(mainLayout);

	mainLayout->setContentsMargins(20,20,20,20);

    auto effect = new QGraphicsDropShadowEffect;
    effect->setOffset(0);
    effect->setBlurRadius(15);
    effect->setColor(QColor(0,0,0));

    holder = new QWidget;
    holderLayout = new QVBoxLayout;
    holder->setLayout(holderLayout);
    holder->setStyleSheet(StyleSheet::QWidgetDark());
    holder->setGraphicsEffect(effect);
    holder->setFixedSize(300,200);
	holderLayout->addStretch();

    buttonWidget = new QWidget; 
	setButtonOrientations(ori);
    buttonWidget->setLayout(buttonHolder);

    holderLayout->addWidget(buttonWidget);
	holderLayout->setContentsMargins(5, 5, 5, 5);
	mainLayout->addWidget(holder);

}

//should add before cancel button
void CustomDialog::addConfirmButton(QString text)
{
    okBtn = new QPushButton(text, this);
	okBtn->setCursor(Qt::PointingHandCursor);
 //   okBtn->setStyleSheet(StyleSheet::QPushButtonGreyscaleBig());
	okBtn->setStyleSheet(StyleSheet::QPushButtonBlueBig());
    if(!buttonHolder->children().contains(okBtn)) buttonHolder->addWidget(okBtn);
    configureConnections();
}

//should be added after confirm button
void CustomDialog::addCancelButton(QString text)
{
    cancelBtn = new QPushButton(text, this);
	cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(StyleSheet::QPushButtonGreyscaleBig());
    if(!buttonHolder->children().contains(cancelBtn)) buttonHolder->addWidget(cancelBtn);
    configureConnections();
}

void CustomDialog::addMessage(QString msg)
{
	auto label = new QLabel(msg, this);
	label->setStyleSheet(StyleSheet::QLabelWhite());
	label->setWordWrap(true);
	label->setIndent(10);
	insertWidget(label);
	holderLayout->insertStretch(index + 1);
}

void CustomDialog::addTitle(QString title)
{
	auto label = new QLabel(title, this);
	auto wid = new QWidget;
	auto layout = new QVBoxLayout;
	wid->setLayout(layout);
	layout->addSpacing(10);
	layout->addWidget(label);

	label->setStyleSheet(StyleSheet::QLabelWhite());
	label->setWordWrap(true);
	label->setAlignment(Qt::AlignCenter);
	//label->setIndent(10);
	QFont f = label->font();
    f.setWeight(QFont::DemiBold);
	f.setPixelSize(20);
	label->setFont(f);
	index++;
	insertWidget(wid, 0);
}

void CustomDialog::addConfirmAndCancelButtons(QString confirmButtonText, QString cancelButtonText)
{
    addConfirmButton(confirmButtonText);
    addCancelButton(cancelButtonText);
    configureConnections();
}

void CustomDialog::setButtonOrientations(Qt::Orientation orientation)
{
	if (orientation == Qt::Horizontal)	buttonHolder = new QHBoxLayout;
	else	buttonHolder = new QVBoxLayout;
}

void CustomDialog::setHolderWidth(int width)
{
	holder->setMinimumWidth(width);
}

void CustomDialog::sendAcceptSignal(bool accept)
{
	if (accept) okBtn->click();
	else cancelBtn->click();
}


//inset in stacked order
void CustomDialog::insertWidget(QWidget *widget, int index)
{
	if(index == -1)		holderLayout->insertWidget(this->index, widget);
	else				holderLayout->insertWidget(index, widget);
	//holderLayout->addSpacing(10);
}

//should be called after buttons added
void CustomDialog::configureConnections()
{
    if(buttonWidget->children().contains(okBtn))
    connect(okBtn, &QPushButton::clicked,[=](){
        emit accepted();
		this->accept();
        close();
    });
    if(buttonWidget->children().contains(cancelBtn))
    connect(cancelBtn, &QPushButton::clicked,[=](){
        emit rejected();
		this->reject();
        close();
    });

}


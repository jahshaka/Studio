/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "createnewdialog.h"
#include <QLayout>
#include <QPainter>
#include "../shadergraphmainwindow.h"
#include <QDebug>
#include <QButtonGroup>
#include <QGraphicsEffect>
#include <QStandardPaths>
#include <QDirIterator>
#include <QJsonDocument>
#include "materials/materialhelper.h"

#include "../core/materialhelper.h"

CreateNewDialog::CreateNewDialog(bool maximized) : QDialog()
{


	if (maximized) createViewWithOptions();
	else createViewWithoutOptions();
    configureStylesheet();
}


CreateNewDialog::~CreateNewDialog()
{
}

void CreateNewDialog::configureStylesheet()
{
	setStyleSheet(
		"QListView::item{ border-radius: 2px; border: 1px solid rgba(0,0,0,1); background: rgba(80,80,80,1); margin: 3px;  }"
		"QListView::item:selected{ background: rgba(65,65,65,1); border: 1px solid rgba(50,150,250,1); }"
		"QListView::item:hover{ background: rgba(55,55,55,1); border: 1px solid rgba(50,150,250,1); }"
		"QListView::text{ top : -6; }"

        "QScrollBar:vertical, QScrollBar:horizontal {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 18px; padding: 4px;}"
        "QScrollBar::handle{ background: rgba(72, 72, 72, 1);	border-radius: 4px; width: 8px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
        "QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
        "QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}"

        "QWidget{background:rgba(32,32,32,1); color:rgba(240,240,240,1); border: 0px solid rgba(0,0,0,0);}"
		"QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9); border-radius : 2px; }"
		"QMenu::item{padding: 4px 5px 4px 10px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"

        "QPushButton{ background: #333; color: #DEDEDE; border : 0; padding: 4px 16px; }"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }"
		"QPushButton:disabled{ color: #444; }"
		"QPushButton:checked{ background-color: rgba(50,150,255,1); }"

		"QLineEdit{background: rgba(0,0,0,0); border-bottom: 1px solid rgba(50,50,50,1);}"
		"QLabel#infoLabel{color: rgba(200,200,200,.5);}"
	);
	
}

void CreateNewDialog::createViewWithOptions()
{
	auto layout = new QVBoxLayout;
	setLayout(layout);
	setMinimumSize(430, 565);

	QSize currentSize(90, 90);

	nameEdit = new QLineEdit;

	optionsScroll = new QWidget;
	presetsScroll = new QWidget;
	options = new QWidget;
	presets = new QWidget;
	auto optionLayout = new QGridLayout;
	auto presetLayout = new QGridLayout;
	infoLabel = new QLabel;

	//controls pading in selection window
	optionLayout->setContentsMargins(10, 10, 10, 10);
	presetLayout->setContentsMargins(10, 10, 10, 10);

	tabbedWidget = new QTabWidget;
	cancel = new QPushButton("Cancel");
	confirm = new QPushButton("Confirm");
	confirm->setDefault(true);
	confirm->setEnabled(false);

	options->setLayout(optionLayout);
	presets->setLayout(presetLayout);
	options->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	presets->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	optionLayout->setSpacing(10);
	presetLayout->setSpacing(10);

	auto buttonHolder = new QWidget;
	auto buttonLayout = new QHBoxLayout;
	buttonHolder->setLayout(buttonLayout);
	buttonLayout->addStretch();
	buttonLayout->addWidget(cancel);
	buttonLayout->addWidget(confirm);

	auto nameHolder = new QWidget;
	auto nameHolderLayout = new QHBoxLayout;
	nameHolder->setLayout(nameHolderLayout);
	nameHolderLayout->addWidget(new QLabel("Name:"));
	nameHolderLayout->addSpacing(5);
	nameHolderLayout->addWidget(nameEdit);

	holder = new QWidget;
	auto holderLayout = new QVBoxLayout;
	holder->setLayout(holderLayout);
	holderLayout->setContentsMargins(0, 20, 0, 0);

	layout->addWidget(holder);
	layout->addWidget(infoLabel);
	layout->addWidget(nameHolder);
	layout->addWidget(buttonHolder);

	auto scrollView = new QScrollArea;
	auto contentHolder = new QWidget;
	auto contentLayout = new QVBoxLayout;
	contentHolder->setLayout(contentLayout);
	scrollView->setWidget(contentHolder);
	scrollView->setWidgetResizable(true);
	scrollView->setContentsMargins(0, 0, 0, 0);
	scrollView->setStyleSheet(
		"QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 10px; }"
		"QScrollBar::handle{ background: rgba(72, 72, 72, 1);	border-radius: 5px;  left: 8px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}"
	);

	auto starterLabel = new QLabel("Starters");
	auto presetLabel = new QLabel("Preset");

	starterLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	presetLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	starterLabel->setStyleSheet("QLabel{ background: rgba(20,20,20,1); padding: 3px; padding-left: 8px; color: rgba(200,200,200,1); }");
	presetLabel->setStyleSheet(starterLabel->styleSheet());

	contentLayout->addWidget(starterLabel);
	contentLayout->addWidget(options);
	contentLayout->addWidget(presetLabel);
	contentLayout->addWidget(presets);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(2);

	holderLayout->addWidget(scrollView);

	nameEdit->setPlaceholderText("Enter name here...");
	nameEdit->setTextMargins(3, 0, 0, 0);
	infoLabel->setAlignment(Qt::AlignCenter);
	infoLabel->setObjectName(QStringLiteral("infoLabel"));

	auto btnGrp = new QButtonGroup;
	btnGrp->setExclusive(true);

	int i = 0;
	int j = 0;

	//set up list options
	auto starterList = getStarterList();
	for (auto tile : starterList) {
		auto item = new OptionSelection(tile);
		optionLayout->addWidget(item, i, j);
		j++;
		if (j % num_of_widgets_per_row == 0) {
			j = 0;
			i++;
		}

		btnGrp->addButton(item);
		connect(item, &OptionSelection::buttonSelected, [=](OptionSelection* button) {
			currentInfoSelected = button->info;
			infoLabel->setText(currentInfoSelected.title + " selected");
		});

	}

	currentInfoSelected = starterList[0];
	infoLabel->setText(currentInfoSelected.title + " selected");

	auto spacerItem = new QWidget;
	spacerItem->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	optionLayout->addWidget(spacerItem);


	i = 0; j = 0;


	for (auto tile : getPresetList()) {
		auto item = new OptionSelection(tile);
		presetLayout->addWidget(item, i, j);
		j++;
		if (j % num_of_widgets_per_row == 0) {
			j = 0;
			i++;
		}
		btnGrp->addButton(item);

		connect(item, &OptionSelection::buttonSelected, [=](OptionSelection* button) {
			currentInfoSelected = button->info;
			infoLabel->setText(currentInfoSelected.title + " selected");
		});


	}

	btnGrp->buttons().at(0)->setChecked(true);

	connect(cancel, &QPushButton::clicked, [=]() {
		this->reject();
	});
	connect(confirm, &QPushButton::clicked, [=]() {
		auto projectName = nameEdit->text();
		this->accept();
		emit confirmClicked(2);
	});

	connect(nameEdit, &QLineEdit::textChanged, [=](QString text) {
		if (text.count() > 0 && currentInfoSelected.name != "") 	confirm->setEnabled(true);
		else 	confirm->setEnabled(false);

		name = text;
		currentInfoSelected.title = text;

	});

	connect(nameEdit, &QLineEdit::returnPressed, [=]() {
		confirm->click();
	});

	holder->setStyleSheet(
		"QWidget{background:rgba(32,32,32,1); color:rgba(240,240,240,1); border: 0px solid rgba(0,0,0,0);}"
		"QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9); border-radius : 2px; }"
		"QMenu::item{padding: 4px 5px 4px 10px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"

		"QTabWidget::pane{border: 0px solid rgba(0,0,0,.5);	border - top: 0px solid rgba(0,0,0,0); border-left : 0px; border-right: 0px;	}"
		"QTabWidget::tab - bar{	left: 1px;	}"
		"QDockWidget::tab{	background:rgba(32,32,32,1);} border: 0px solid rgba(0,0,0,0);"

		"QPushButton{ background: #777; color: #DEDEDE; border : 0; padding: 4px 16px; }"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }"
		"QPushButton:disabled{ color: #444; }"
		"QPushButton:checked{ background-color: rgba(50,150,255,1); }"
	);

	tabbedWidget->setStyleSheet(
		"QTabWidget::pane{	border: 0px solid rgba(0, 0, 0, .5); border-top: 1px solid rgba(0, 0, 0, .4); border-bottom: 1px solid rgba(0,0,0,.4);}"
		"QTabBar::tab{	background: rgba(21, 21, 21, .7); color: rgba(250, 250, 250, .9); font - weight: 400; font - size: 13em; padding: 5px 22px 5px 22px; }"
		"QTabBar::tab:selected{ color: rgba(255, 255, 255, .99); border-top: 2px solid rgba(50,150,250,.8); }"
		"QTabBar::tab:!selected{ background: rgba(55, 55, 55, .99); border : 1px solid rgba(21,21,21,.4); color: rgba(200,200,200,.5); }"
	);
}

void CreateNewDialog::createViewWithoutOptions()
{
	auto layout = new QVBoxLayout;
	setLayout(layout);
	setMinimumSize(430, 65);

	nameEdit = new QLineEdit;
	nameEdit->setPlaceholderText("Enter name here...");
	nameEdit->setTextMargins(3, 0, 0, 0);

	cancel = new QPushButton("Cancel");
	confirm = new QPushButton("Confirm");
	confirm->setDefault(true);
	confirm->setEnabled(false);

	auto buttonHolder = new QWidget;
	auto buttonLayout = new QHBoxLayout;
	buttonHolder->setLayout(buttonLayout);
	buttonLayout->addStretch();
	buttonLayout->addWidget(cancel);
	buttonLayout->addWidget(confirm);

	auto nameHolder = new QWidget;
	auto nameHolderLayout = new QHBoxLayout;
	nameHolder->setLayout(nameHolderLayout);
	nameHolderLayout->addWidget(new QLabel("Name:"));
	nameHolderLayout->addSpacing(5);
	nameHolderLayout->addWidget(nameEdit);

	layout->addWidget(nameHolder);
	layout->addWidget(buttonHolder);

	currentInfoSelected = getStarterList().at(0);

	connect(cancel, &QPushButton::clicked, [=]() {
		this->reject();
	});
	connect(confirm, &QPushButton::clicked, [=]() {
		auto projectName = nameEdit->text();
		this->accept();
		emit confirmClicked(2);
	});

	connect(nameEdit, &QLineEdit::textChanged, [=](QString text) {
		if (text.count() > 0) 	confirm->setEnabled(true);
		else 	confirm->setEnabled(false);
		name = text;

		currentInfoSelected.title = text;
	});

	connect(nameEdit, &QLineEdit::returnPressed, [=]() {
		confirm->click();
	});

}

QList<NodeGraphPreset> CreateNewDialog::getPresetList()
{
	QList<NodeGraphPreset> presetsList;
	NodeGraphPreset graphPreset;
	graphPreset.name = "Checker Board";
	graphPreset.title = "Chacker Board";
	graphPreset.templatePath = "checker.effect";
	graphPreset.iconPath = "checkerThumb.png";
	graphPreset.list.append("checker.jpg");
	presetsList.append(graphPreset);
	graphPreset.list.clear();

	graphPreset.name = "Grass";
	graphPreset.title = "Grass Template";
	graphPreset.templatePath = "grass.effect";
	graphPreset.iconPath = "grassThumb.png";
	graphPreset.list.append("grass.jpg");
	presetsList.append(graphPreset);
	graphPreset.list.clear();

	graphPreset.name = "Gold";
	graphPreset.title = "Gold Template";
	graphPreset.templatePath = "gold.effect";
	graphPreset.iconPath = "goldThumb.png";
	//graphPreset.list.append("assets/grass.jpg");
	presetsList.append(graphPreset);
	graphPreset.list.clear();

	return presetsList;
}

QList<NodeGraphPreset> CreateNewDialog::getAdditionalPresetList()
{
	QList<NodeGraphPreset> presetsList;
	NodeGraphPreset graphPreset;
	// create constants for this
	QString prefix(QString("materials_to_graph") + QDir::separator());
	auto filePath = MaterialHelper::assetPath(prefix);
	QDirIterator it(filePath);

	while (it.hasNext()) {

		QFile file(it.next());
        qDebug() << file.fileName();

#ifdef Q_OS_MAC
        if (file.fileName().split('.')[2] == "effect") {
            QFileInfo fileInfo(file.fileName().split('.')[1]);
#else
        if (file.fileName().split('.')[1] == "effect") {
            QFileInfo fileInfo(file.fileName().split('.')[0]);
#endif


			graphPreset.name = fileInfo.fileName();
			graphPreset.title = graphPreset.name + " Template";
			graphPreset.templatePath = prefix + graphPreset.name + ".effect";
			graphPreset.iconPath = prefix + graphPreset.name.toLower() + ".png";
			graphPreset.list.append(prefix + graphPreset.name.toLower() + " diff.jpg");
			graphPreset.list.append(prefix + graphPreset.name.toLower() + " spec.jpg");
			graphPreset.list.append(prefix + graphPreset.name.toLower() + " norm.png");

			presetsList.append(graphPreset);
			graphPreset.list.clear();
		}
	}
	return presetsList;
}

QList<NodeGraphPreset> CreateNewDialog::getStarterList()
{
	NodeGraphPreset graphPreset;
	QList<NodeGraphPreset> list;
	graphPreset.name = "Default";
	graphPreset.title = "Default Template";
    graphPreset.templatePath = "default.effect";
	graphPreset.iconPath = "default.png";
	list.append(graphPreset);
	graphPreset.list.clear();

	graphPreset.name = "Basic";
	graphPreset.title = "Basic Template";
    graphPreset.templatePath = "basic.effect";
	graphPreset.iconPath = "basic.png";
	list.append(graphPreset);
	graphPreset.list.clear();

	graphPreset.name = "Texture";
	graphPreset.title = "Texture Template";
    graphPreset.templatePath = "texture.effect";
	graphPreset.iconPath = "texture.png";
    graphPreset.list.append("wood.jpg");
	list.append(graphPreset);
	graphPreset.list.clear();

	return list;
}


OptionSelection::OptionSelection(NodeGraphPreset node) : QPushButton()
{
	setFixedSize(120, 120);
	checkedIconIcon.load(":/icons/checked.png");
	checkedIconIcon = checkedIconIcon.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	info = node;

	if (info.iconPath == "") setIcon(QIcon(info.iconPath));
	else setIcon(QIcon(MaterialHelper::assetPath(info.iconPath)));
	setIconSize(QSize(120,120));
	setCheckable(true);
	setAutoExclusive(true);

	auto layout = new QVBoxLayout;
    setLayout(layout);
    auto label = new QLabel;
    auto name = new QLabel;
    name->setAlignment(Qt::AlignBottom | Qt::AlignHCenter);
    name->setText(node.title);

	auto font = name->font();
    font.setWeight(QFont::Medium);
    name->setFont(font);

	auto effect = new QGraphicsDropShadowEffect;
	effect->setBlurRadius(15);
	effect->setXOffset(0);
	effect->setYOffset(1);
	effect->setColor(QColor(0, 0, 0, 255));
	name->setGraphicsEffect(effect);

    layout->addWidget(name);

    setStyleSheet("QPushButton{ background: #333; color: #DEDEDE; border : 0px; padding: 4px 16px; border-radius: 3px;}"
                  "QPushButton:hover{ background-color: #555; }"
                  "QPushButton:pressed{ background-color: #444; }"
                  "QPushButton:disabled{ color: #444; }"
                  "QPushButton:checked{ background-color: rgba(50,150,250,.8); }"
                  "QLabel{ border: 0; background: rgba(0,0,0,0); }"
                  );

	connect(this, &OptionSelection::clicked, [=]() {
		emit buttonSelected(this);
	});

}

void OptionSelection::paintEvent(QPaintEvent *event)
{
    QPushButton::paintEvent(event);
   
    
	if (isChecked()) {
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::Antialiasing);
		painter.drawPixmap(width() - 25, 5, 23, 23, checkedIconIcon);
	}

}

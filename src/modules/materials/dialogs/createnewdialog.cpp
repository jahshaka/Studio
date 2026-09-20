/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "createnewdialog.h"
#include <QLayout>
#include <QPainter>
#include "../effectspage.h"
#include <QDebug>
#include <QButtonGroup>
#include <QGraphicsEffect>
#include <QStandardPaths>
#include <QDirIterator>
#include <QJsonDocument>
#include "irisgl/import/materialhelper.h"

#include "../core/materialhelper.h"
#include "data/constants.h"
#include "data/materialpreset.h"
#include "io/materialpresets.h"
#include "ui/style/stylesheet.h"

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
	setStyleSheet(StyleSheet::CreateNewTiles());
	
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
	scrollView->setStyleSheet(StyleSheet::EffectsNodeTilesScrollBar());

	auto starterLabel = new QLabel("Starters");
	auto presetLabel = new QLabel("Preset");

	starterLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	presetLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	starterLabel->setStyleSheet(StyleSheet::CreateNewSectionLabel());
	presetLabel->setStyleSheet(StyleSheet::CreateNewSectionLabel());

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
	const auto tiles = presetTiles();
	for (const auto &tile : tiles) {
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

	if (!tiles.isEmpty()) {
		currentInfoSelected = tiles.first();
		infoLabel->setText(currentInfoSelected.title + " selected");
	}

	auto spacerItem = new QWidget;
	spacerItem->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	optionLayout->addWidget(spacerItem);

	// (The second grid is gone with the second preset family: there is one
	// list, so there is one section.)
	presetLabel->hide();
	presets->hide();

	if (!btnGrp->buttons().isEmpty()) btnGrp->buttons().at(0)->setChecked(true);

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

	holder->setStyleSheet(StyleSheet::CreateNewHolder());

	tabbedWidget->setStyleSheet(StyleSheet::CreateNewTabs());
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

	const auto tiles = presetTiles();
	if (!tiles.isEmpty()) currentInfoSelected = tiles.first();

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

QList<NodeGraphPreset> CreateNewDialog::presetTiles()
{
	// ONE LIST (PRESET-UNIFY-1). Three functions stood here: a hand-written
	// "presets" list of five `.effect` templates, a hand-written "starters"
	// list of three more, and a directory walk of nine under
	// `materials_to_graph/` — seventeen graph templates that were the SECOND
	// preset family, shown beside the shipped material presets in the same
	// drawer ("Brick" above "Brick PBR"). They are the same twenty presets
	// now, read once from `app/content/materials`, and each one carries the
	// graph a new material is based on.
	QList<NodeGraphPreset> tiles;
	for (const MaterialPreset &preset : MaterialPresets::all()) {
		NodeGraphPreset tile;
		tile.name = preset.name;
		tile.title = preset.name;
		tile.iconPath = preset.icon;
		tile.guid = Constants::Reserved::DefaultMaterials.key(preset.name);
		tiles.append(tile);
	}
	return tiles;
}

OptionSelection::OptionSelection(NodeGraphPreset node) : QPushButton()
{
	setFixedSize(120, 120);
	checkedIconIcon.load(":/icons/checked.png");
	checkedIconIcon = checkedIconIcon.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	info = node;

	// An ABSOLUTE path: the preset's shipped tile (PRESET-UNIFY-1). It used to
	// be relative to the shadergraph asset folder, which only the deleted
	// templates lived in.
	setIcon(QIcon(info.iconPath));
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

    setStyleSheet(StyleSheet::CreateNewButtons());

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

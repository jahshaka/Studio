/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QMenu>

#include "assetgriditem.h"

// local
AssetGridItem::AssetGridItem(QJsonObject details, QImage image, QJsonObject properties, QJsonObject tags, QWidget *parent) : QWidget(parent) {
	this->metadata = details;
	this->sceneProperties = properties;
	this->tags = tags;
	url = details["icon_url"].toString();
	selected = false;
	auto layout = new QGridLayout;
    layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	pixmap = QPixmap::fromImage(image);
	gridImageLabel = new QLabel;
	gridImageLabel->setPixmap(pixmap.scaledToHeight(116, Qt::SmoothTransformation));
	gridImageLabel->setAlignment(Qt::AlignCenter);

	layout->addWidget(gridImageLabel, 0, 0);
	textLabel = new QLabel(QFileInfo(details["name"].toString()).baseName());

	textLabel->setWordWrap(true);
	textLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	layout->addWidget(textLabel, 1, 0);

	setMinimumWidth(128);
	setMaximumWidth(128);
	setMinimumHeight(142);
	setMaximumHeight(142);

	textLabel->setStyleSheet("color: #ddd; font-size: 12px; background: #1e1e1e;"
		"border-left: 3px solid rgba(0, 0, 0, 3%); border-bottom: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");
	gridImageLabel->setStyleSheet("border-left: 3px solid rgba(0, 0, 0, 3%); border-top: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");

	setStyleSheet("background: #272727");
	setLayout(layout);
	setCursor(Qt::PointingHandCursor);
	setContextMenuPolicy(Qt::CustomContextMenu);

	connect(this, SIGNAL(hovered()), SLOT(dimHighlight()));
	connect(this, SIGNAL(left()), SLOT(noHighlight()));

	connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), SLOT(projectContextMenu(QPoint)));
}

void AssetGridItem::projectContextMenu(const QPoint &pos)
{
	QMenu menu("Context Menu", this);
	menu.setStyleSheet(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
		"QMenu::item : disabled { color: #555; }"
	);

	QAction add("Add to Project", this);
	connect(&add, &QAction::triggered, this, [this]() {
		emit addAssetItemToProject(this);
	});
	menu.addAction(&add);

	QAction change("Change Collections", this);
	connect(&change, &QAction::triggered, this, [this]() {
		emit changeAssetCollection(this);
	});
	menu.addAction(&change);

	QAction remove("Delete", this);
	connect(&remove, &QAction::triggered, this, [this]() {
		emit removeAssetFromProject(this);
	});
	menu.addAction(&remove);

	menu.exec(mapToGlobal(pos));
}

void AssetGridItem::setTile(QPixmap pix) {
	pixmap = pix;
	gridImageLabel->setPixmap(pixmap.scaledToHeight(116, Qt::SmoothTransformation));
	gridImageLabel->setAlignment(Qt::AlignCenter);
}

void AssetGridItem::enterEvent(QEvent *event) {
    QWidget::enterEvent(static_cast<QEnterEvent*>(event));
	emit hovered();
}

void AssetGridItem::leaveEvent(QEvent *event) {
	QWidget::leaveEvent(event);
	emit left();
}

void AssetGridItem::mousePressEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton && !event->modifiers().testFlag(Qt::ShiftModifier)) {
		emit singleClicked(this);
	}

	if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier)) {
		emit specialClicked(this);
	}

	if (event->button() == Qt::RightButton) {
		emit contextClicked(this);
	}
}

void AssetGridItem::dimHighlight() {
    if (!selected) {
        textLabel->setStyleSheet("color: #ddd; font-size: 12px; background: #1e1e1e;"
            "border-left: 3px solid rgba(0, 0, 0, 10%); border-bottom: 3px solid rgba(0, 0, 0, 10%); border-right: 3px solid rgba(0, 0, 0, 10%)");
        gridImageLabel->setStyleSheet("border-left: 3px solid rgba(0, 0, 0, 10%); border-top: 3px solid rgba(0, 0, 0, 10%); border-right: 3px solid rgba(0, 0, 0, 10%)");
    }
}

void AssetGridItem::noHighlight() {
    if (!selected) {
        textLabel->setStyleSheet("color: #ddd; font-size: 12px; background: #1e1e1e;"
            "border-left: 3px solid rgba(0, 0, 0, 3%); border-bottom: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");
        gridImageLabel->setStyleSheet("border-left: 3px solid rgba(0, 0, 0, 3%); border-top: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");
    }
}

void AssetGridItem::highlight(bool highlight) {
	selected = highlight;
	if (selected) {
		textLabel->setStyleSheet("color: #ddd; font-size: 12px; background: #1e1e1e;"
			"border-left: 3px solid #3498db; border-bottom: 3px solid #3498db; border-right: 3px solid #3498db");
		gridImageLabel->setStyleSheet("border-left: 3px solid #3498db; border-top: 3px solid #3498db; border-right: 3px solid #3498db");
	}
	else {
		dimHighlight();
	}
}

void AssetGridItem::updateMetadata(QJsonObject details, QJsonObject tags)
{
	this->metadata = details;
	this->tags = tags;
	textLabel->setText(details["name"].toString());
}

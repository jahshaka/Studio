/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QDataStream>
#include <QDrag>
#include <QIODevice>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QTimer>

#include "ui/controls/assetgriditem.h"
#include "ui/style/stylesheet.h"

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

	textLabel->setStyleSheet(StyleSheet::AssetGridItemLabel("rgba(0, 0, 0, 3%)"));
	gridImageLabel->setStyleSheet(StyleSheet::AssetGridItemThumbnail("rgba(0, 0, 0, 3%)"));

	setStyleSheet("background: #272727");
	setLayout(layout);
	setCursor(Qt::PointingHandCursor);
	setContextMenuPolicy(Qt::CustomContextMenu);

	connect(this, SIGNAL(hovered()), SLOT(dimHighlight()));
	connect(this, SIGNAL(left()), SLOT(noHighlight()));

	connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), SLOT(projectContextMenu(QPoint)));
}

void AssetGridItem::setDrawerProvider(std::function<QVector<DrawerEntry>()> provider)
{
	drawerProvider = provider;
}

void AssetGridItem::projectContextMenu(const QPoint &pos)
{
	QMenu menu("Context Menu", this);
	menu.setStyleSheet(StyleSheet::QMenuDark());

	QAction add("Add to Project", this);
	connect(&add, &QAction::triggered, this, [this]() {
		emit addAssetItemToProject(this);
	});
	menu.addAction(&add);

	// Move to ▸ — the drawer tree, indented (replaces the old Change
	// Collections dialog; ASSET_DRAWERS_SPEC §1).
	QMenu *moveTo = menu.addMenu("Move to");
	moveTo->setStyleSheet(StyleSheet::QMenuDark());
	if (drawerProvider) {
		const int currentDrawer = metadata["collection"].toInt();
		for (const auto &entry : drawerProvider()) {
			QAction *action = moveTo->addAction(entry.second);
			action->setEnabled(entry.first != currentDrawer);
			const int drawerId = entry.first;
			connect(action, &QAction::triggered, this, [this, drawerId]() {
				emit moveAssetToDrawer(this, drawerId);
			});
		}
	}
	moveTo->setEnabled(!moveTo->isEmpty());

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

// Tile interaction flip (ASSET_DRAWERS_SPEC §1): press marks a drag candidate;
// moving past startDragDistance starts a drag carrying the asset guid; a plain
// press+release is only a subtle highlight. Double-click selects and loads.
void AssetGridItem::mousePressEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton && !event->modifiers().testFlag(Qt::ShiftModifier)) {
		pressPos = event->pos();
		dragCandidate = true;
	}

	if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier)) {
		emit specialClicked(this);
	}

	if (event->button() == Qt::RightButton) {
		emit contextClicked(this);
	}
}

void AssetGridItem::mouseMoveEvent(QMouseEvent *event) {
	if (dragCandidate && (event->buttons() & Qt::LeftButton)
	    && (event->pos() - pressPos).manhattanLength() >= QApplication::startDragDistance()) {
		dragCandidate = false;
		startDrag();
		return;
	}
	QWidget::mouseMoveEvent(event);
}

void AssetGridItem::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton && dragCandidate) {
		dragCandidate = false;
		emit singleClicked(this);
	}
	QWidget::mouseReleaseEvent(event);
}

void AssetGridItem::mouseDoubleClickEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton && !event->modifiers().testFlag(Qt::ShiftModifier)) {
		dragCandidate = false;
		emit doubleClicked(this);
	}
	QWidget::mouseDoubleClickEvent(event);
}

void AssetGridItem::startDrag()
{
	if (metadata.isEmpty()) return;

	// The assetwidget mime (project.h roles): type at 0, name at 1, guid at 3 —
	// the drawers tree and any existing model-data drop handler read the same
	// payload.
	QByteArray mdata;
	QDataStream stream(&mdata, QIODevice::WriteOnly);
	QMap<int, QVariant> roleDataMap;
	roleDataMap[0] = QVariant(metadata["type"].toInt());
	roleDataMap[1] = QVariant(metadata["name"].toString());
	roleDataMap[2] = QVariant(QString());
	roleDataMap[3] = QVariant(metadata["guid"].toString());
	stream << roleDataMap;

	auto drag = new QDrag(this);
	auto mimeData = new QMimeData;
	mimeData->setData(QStringLiteral("application/x-qabstractitemmodeldatalist"), mdata);
	drag->setMimeData(mimeData);
	if (!pixmap.isNull()) drag->setPixmap(pixmap.scaledToHeight(64, Qt::SmoothTransformation));
	drag->exec(Qt::MoveAction | Qt::CopyAction);
}

void AssetGridItem::showLoadingOverlay()
{
	if (!loadingOverlay) {
		loadingOverlay = new QLabel(this);
		loadingOverlay->setAlignment(Qt::AlignCenter);
		loadingOverlay->setText("Loading…");
		loadingPulse = new QTimer(this);
		loadingPulse->setInterval(350);
		connect(loadingPulse, &QTimer::timeout, this, [this]() {
			pulsePhase = !pulsePhase;
			loadingOverlay->setStyleSheet(pulsePhase
				? "background: rgba(0, 0, 0, 55%); color: #3498db; font-size: 12px;"
				: "background: rgba(0, 0, 0, 55%); color: #ffffff; font-size: 12px;");
		});
	}
	pulsePhase = false;
	loadingOverlay->setStyleSheet("background: rgba(0, 0, 0, 55%); color: #ffffff; font-size: 12px;");
	loadingOverlay->setGeometry(rect());
	loadingOverlay->show();
	loadingOverlay->raise();
	loadingPulse->start();
}

void AssetGridItem::hideLoadingOverlay()
{
	if (!loadingOverlay) return;
	loadingPulse->stop();
	loadingOverlay->hide();
}

void AssetGridItem::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (loadingOverlay && loadingOverlay->isVisible()) loadingOverlay->setGeometry(rect());
}

void AssetGridItem::dimHighlight() {
    if (!selected) {
        textLabel->setStyleSheet(StyleSheet::AssetGridItemLabel("rgba(0, 0, 0, 10%)"));
        gridImageLabel->setStyleSheet(StyleSheet::AssetGridItemThumbnail("rgba(0, 0, 0, 10%)"));
    }
}

void AssetGridItem::noHighlight() {
    if (!selected) {
        textLabel->setStyleSheet(StyleSheet::AssetGridItemLabel("rgba(0, 0, 0, 3%)"));
        gridImageLabel->setStyleSheet(StyleSheet::AssetGridItemThumbnail("rgba(0, 0, 0, 3%)"));
    }
}

void AssetGridItem::highlight(bool highlight) {
	selected = highlight;
	if (selected) {
		textLabel->setStyleSheet(StyleSheet::AssetGridItemLabel("#3498db"));
		gridImageLabel->setStyleSheet(StyleSheet::AssetGridItemThumbnail("#3498db"));
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

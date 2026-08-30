/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETGRIDITEM_HPP
#define ASSETGRIDITEM_HPP

#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>
#include <QResizeEvent>
#include <QGridLayout>
#include <QScrollArea>
#include <qdebug.h>
#include <QLabel>
#include <QButtonGroup>

#include <QWidget>
#include <functional>

#include "irisgl/core/irisutils.h"

class QTimer;

class AssetGridItem : public QWidget
{
	Q_OBJECT

public:
	QString url;
	QPixmap pixmap;
	QLabel *gridImageLabel;
	QLabel *textLabel;
	bool selected;
	QJsonObject metadata;
	QJsonObject sceneProperties;
	QJsonObject tags;

	/// One drawer entry for the Move to ▸ submenu: id + display name
	/// (pre-indented to show nesting).
	using DrawerEntry = QPair<int, QString>;

	AssetGridItem() = default;
	AssetGridItem(QJsonObject details, QImage image, QJsonObject properties, QJsonObject tags, QWidget *parent = Q_NULLPTR);
	void setTile(QPixmap pix);
	void highlight(bool);
	void updateMetadata(QJsonObject details, QJsonObject tags);

	/// AssetView injects the live drawer list; the context menu's Move to ▸
	/// submenu enumerates it on open (ASSET_DRAWERS_SPEC §1).
	void setDrawerProvider(std::function<QVector<DrawerEntry>()> provider);

	/// The double-click loading pulse (§1 tile interaction flip): shown from
	/// the click until the viewer reports the load finished.
	void showLoadingOverlay();
	void hideLoadingOverlay();

	void enterEvent(QEvent *event);
	void leaveEvent(QEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

public slots:
	void projectContextMenu(const QPoint &pos);
	void dimHighlight();
	void noHighlight();

signals:
	void hovered();
	void left();
	void singleClicked(AssetGridItem*);		// plain click: subtle highlight, nothing loads
	void doubleClicked(AssetGridItem*);		// select + load into the preview
	void specialClicked(AssetGridItem*);	// bypass loading asset and add to scene
	void contextClicked(AssetGridItem*);	// use this exclusively for right clicks

	void addAssetItemToProject(AssetGridItem*);
	void moveAssetToDrawer(AssetGridItem*, int drawerId);
	void removeAssetFromProject(AssetGridItem*);
	void rebuildThumbnail(AssetGridItem*);

private:
	void startDrag();

	QPoint pressPos;
	bool dragCandidate = false;

	std::function<QVector<DrawerEntry>()> drawerProvider;

	QLabel *loadingOverlay = nullptr;
	QTimer *loadingPulse = nullptr;
	bool pulsePhase = false;
};

#endif // ASSETGRIDITEM_HPP

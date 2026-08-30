/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/style/stylesheet.h"
#include <QDebug>
#include <QButtonGroup>
#include <QAbstractButton>

// ---- Qlementine kill switch -------------------------------------------------
// When the Qlementine QStyle is active (the default since 2026-08-31), every
// classic stylesheet getter returns an empty sheet: an empty string never
// interposes QStyleSheetStyle, so Qlementine keeps full control of the widget.
// When the archived "Jahshaka Classic" theme is selected, the flag is true and
// every getter returns exactly what it always did — bit-for-bit Classic.
// Set once at startup by ThemeManager::applyAtStartup, before any widget exists.
static bool s_classicThemeActive = false;

bool StyleSheet::classicThemeActive() { return s_classicThemeActive; }
void StyleSheet::setClassicThemeActive(bool active) { s_classicThemeActive = active; }

#define JAH_CLASSIC_ONLY if (!s_classicThemeActive) return QString();
// -----------------------------------------------------------------------------


const QString StyleSheet::QPushButtonBlue()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QPushButton{ background : rgba(50,148,213,0.9); color: #DEDEDE; border : 0; padding: 4px 16px; }"
		"QPushButton:hover{ background-color: rgba(60,158,223,0.9); }"
		"QPushButton:pressed{ background-color: rgba(40,138,203,0.9); }"
		"QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }"
	);
}

const QString StyleSheet::QPushButtonBlueBig()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QPushButton{ background : rgba(50,148,213,0.9); color: #DEDEDE; border : 0; padding: 8px 22px; }"
		"QPushButton:hover{ background-color: rgba(60,158,223,0.9); }"
		"QPushButton:pressed{ background-color: rgba(40,138,203,0.9); }"
		"QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }"
	);
}

const QString StyleSheet::QPushButtonInvisible() {
	JAH_CLASSIC_ONLY
    return QString(
        "QPushButton{background : rgba(0,0,0,0); border : 0px; }"
    );
}

const QString StyleSheet::QSpinBox() {
	JAH_CLASSIC_ONLY
    return QString(
		"QAbstractSpinBox { background: rgba(21,21,21,1); color:rgba(255,255,255,.8); padding: 4px; padding-right: 0px; margin-right: 0px; border : 0; }"
		"QAbstractSpinBox::down-button, QAbstractSpinBox::up-button { background: rgba(0,0,0,0); border : 1px solid rgba(0,0,0,0); }"
        
    );
}

const QString StyleSheet::QSlider() {
	JAH_CLASSIC_ONLY
    return QString(
        "QSlider::sub-page {    border: 0px solid transparent;    height: 2px;    background: #3498db;    margin: 2px 0;}"
        "QSlider::groove:horizontal { border: 0px solid transparent; height: 4px; background: #1e1e1e;   margin: 1px 0; border-radius: .5px; }"
        "QSlider::handle:horizontal {    background-color: #CCC;    width: 12px;    border: 1px solid #1e1e1e;    margin: -5px 0px;   border-radius:7px;}"
        "QSlider::handle:horizontal:pressed {    background-color: #AAA;    width: 12px;   border: 1px solid #1e1e1e;    margin: -5px 0px;    border-radius: 7px;}"
        "QSlider::handle:horizontal:disabled {    background-color: #bbbbbb;    width: 12px;    border: 0px solid transparent;    margin: -1px -1px;    border-radius: 4px;}"
        "QSlider::groove:vertical {background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,stop: 0 white, stop: 1 black); border-radius: 2px; width: 13px; }"
        " QSlider::handle:vertical {height: 3px; width:1px; margin: -2px 0px; background: rgba(50,148,213,0.9); }"
        " QSlider::add-page:vertical, QSlider::sub-page:vertical {background: rgba(0,0,0,0); border-radius: 1px;}"
        
    );
}

const QString StyleSheet::QLineEdit() {
	JAH_CLASSIC_ONLY
    return QString(
        "QLineEdit { color: rgba(255,255,255,.9); background: rgba(51,51,51,0.5); border: 0px solid rgba(0,0,0,0); selection-background-color: #808080; padding : 5px;}"

    );
}

const QString StyleSheet::QPushButtonGreyscale() {
	JAH_CLASSIC_ONLY
    return QString(
        "QPushButton{ background-color: #333; color: #DEDEDE; border : 0; padding: 4px 16px; }"
        "QPushButton:hover{ background-color: #555; }"
        "QPushButton:pressed{ background-color: #444; }"
        "QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }"
    );
}

const QString StyleSheet::QPushButtonGreyscaleBig()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QPushButton{ background-color: #333; color: #DEDEDE; border : 0; padding: 8px 22px; }"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }"
		"QPushButton:checked{border: 0px solid rgba(0,0,0,.3); background: rgba(50,148,213,0.9); color: rgba(255,255,255,.9); }"
	);
}

const QString StyleSheet::QWidgetDark() {
	JAH_CLASSIC_ONLY
    return QString(
        "QWidget{ background: rgba(26,26,26,1);border: 1px solid rgba(0,0,0,0); padding:0px; spacing : 0px;}"
    );
}

const QString StyleSheet::QWidgetTransparent()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QWidget{ background: rgba(26,26,26,0);border: 1px solid rgba(0,0,0,0); padding:0px; spacing : 0px;}"
	);
}

const QString StyleSheet::QLabelWhite() {
	JAH_CLASSIC_ONLY
    return QString(
        "QLabel{ color : rgba(255, 255,255, .9); }"
    );
}

const QString StyleSheet::QLabelBlack()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QLabel{ color : rgba(20, 20, 20, .9); }"
	);
}

const QString StyleSheet::QComboBox() {
	JAH_CLASSIC_ONLY
    return QString(
		"QComboBox  {    background-color: #1A1A1A;   border: 0;    outline: none; padding: 3px 10px; color: rgba(255,255,255,.9); }"
		"QComboBox:editable {}"
		"QComboBox QAbstractItemView::item {    show-decoration-selected: 1;  padding: 6px; }"
		"QComboBox:!editable, QComboBox::drop-down:editable {     background: #1A1A1A;}"
		"QComboBox:!editable:on, QComboBox::drop-down:editable:on {    background: #1A1A1A;}"
		"QComboBox QAbstractItemView {    background-color: #1A1A1A; color: rgba(255,255,255,.9);    selection-background-color: #404040;    border: 0;    outline: none; padding: 4px 10px; }"
		"QComboBox QAbstractItemView::item {    border: none; padding: 4px 10px;}"
		"QComboBox QAbstractItemView::item:selected {    background: #404040;    padding-left: 5px;}"
		"QComboBox::drop-down {    subcontrol-origin: padding;    subcontrol-position: top right;    width: 18px;    border-left-width: 1px;}"
		"QComboBox::down-arrow {    image: url(:/icons/down_arrow_check.png);	width: 18px;	height: 14px;} "
		"QComboBox::down-arrow:!enabled {    image: url(:/icons/down_arrow_check_disabled.png);    width: 18px;    height: 14px;}"
	);
}

const QString StyleSheet::QPushButtonRounded(int size) {
	JAH_CLASSIC_ONLY
    return QString(
        "QPushButton{border : 0px; radius : "+ QString::number(size/2)+" }"
    );
}


const QString StyleSheet::QPushButtonGrouped() {
	JAH_CLASSIC_ONLY
	return QString(
		"QAbstractButton{ background:rgba(51,51,51,.5); color: rgba(190,190,190,1); border : 0 ; padding: 4px 16px;} "
		"QAbstractButton:checked{ background : rgba(50,150,250,1);}"
		"QAbstractButton:hover{background: rgba(50,50,50,1);}"
		"QAbstractButton:pressed{background:rgba(61,61,61,.9);}"
	);
}

const QString StyleSheet::QPushButtonGroupedBig() {
	JAH_CLASSIC_ONLY
	return QString(
		"QAbstractButton{ background:rgba(51,51,51,.5); color: rgba(190,190,190,1); border : 0 ; padding: 12px 30px;} "
		"QAbstractButton:checked{ background : rgba(50,150,250,1);}"
		"QAbstractButton:hover{background: rgba(50,50,50,1);}"
		"QAbstractButton:pressed{background:rgba(61,61,61,.9);}"
	);
}

const QString StyleSheet::QPushButtonDanger()
{
	JAH_CLASSIC_ONLY
	return QPushButtonGrouped() + QString(
		"QAbstractButton{background : rgba(200,40,40,1);}"
	);
}

const QString StyleSheet::QCheckBox()
{
	JAH_CLASSIC_ONLY
	return QString(
		//"QCheckBox { width: 16px; height :16px; }"
		"QCheckBox::indicator {   width: 18px;   height: 18px; margin : 0px; padding :0px; right : -5px; }"
		"QCheckBox::indicator::unchecked {	image: url(:/icons/check-unchecked.png);}"
		"QCheckBox::indicator::checked {image: url(:/icons/check-checked.png);}"
	);
}

const QString StyleSheet::QSplitter()
{
	JAH_CLASSIC_ONLY
	return QString(
	"QSplitter::handle:horizontal{}"
	);
}

const QString StyleSheet::QAbstractScrollArea()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QAbstractScrollArea{ background : rgba(51,51,51,.5); color : rgba(255,255,255,.9); border : 0; padding : 0px 12px;  }"
		"QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 16px; padding: 4px;}"
		"QScrollBar::handle:vertical{ background: rgba(72, 72, 72, 1);	border-radius : 4px; width: 10px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		
		"QScrollBar:horizontal {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 24px; padding: 4px;}"
		"QScrollBar::handle:horizontal{ background: rgba(72, 72, 72, 1);	border-radius: 8px; height: 10px; }"
		"QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:horizontal, QScrollBar::down-arrow:horizontal {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}"
	);
}

const QString StyleSheet::QMenu()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9); border-radius : 2px; }"
		"QMenu::item{padding: 6px 20px 6px 14px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"
	);
}

/* -------------------------------------------------------------------------
   Blocks centralized from the widgets that used to carry them inline.
   The CSS text is byte-identical to what those call sites passed before.
   ------------------------------------------------------------------------- */

const QString StyleSheet::QMenuDark()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
		"QMenu::item:disabled { color: #555; }"
	);
}

const QString StyleSheet::QMenuDarkGrid()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
		"QMenu::item:disabled { color: #555; }"
	);
}

const QString StyleSheet::QMenuDarkPadded()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 16px 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; }"
		"QMenu::item:disabled { color: #555; }"
	);
}

const QString StyleSheet::QMenuFlat()
{
	JAH_CLASSIC_ONLY
	return QString(
			"QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9);}"
			"QMenu::item{padding: 2px 5px 2px 20px;	}"
			"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
			"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"
		//	"QMenu::indicator{ width : 13; height : 10; border-radius: 3px; background: rgba(53,53,53,.9);}"
			//"QMenu::indicator:checked{background: rgba(40,128, 185,.9);}"
	);
}

const QString StyleSheet::AssetWidgetFilterPane()
{
	JAH_CLASSIC_ONLY
	return QString(
		"#filterPane { background: #1E1E1E; border-bottom: 1px solid #111; }"
		"QLabel { font-size: 12px; margin-right: 8px; }"
		"QPushButton[accessibleName=\"filterObj\"] { border-radius: 0; padding: 10px 8px; }"
		"QComboBox { background: #222; border-radius: 1px; color: #BBB; padding: 0 12px; min-height: 24px; min-width: 64px; border: 1px solid #111;}"
		"QComboBox::drop-down { border: 0; margin: 0; padding: 0; min-height: 20px; }"
		"QComboBox::down-arrow { image: url(:/icons/down_arrow_check.png); width: 18px; height: 14px; }"
		"QComboBox::down-arrow:!enabled { image: url(:/icons/down_arrow_check_disabled.png); width: 18px; height: 14px; }"
		"QComboBox QAbstractItemView::item { min-height: 28px; selection-background-color: #404040; color: #cecece; }"
		"QComboBox QAbstractItemView { background-color: #1A1A1A; selection-background-color: #404040; border: 0; outline: none; }"
		"QComboBox QAbstractItemView::item:selected { background: #404040; }"
	);
}

const QString StyleSheet::AssetWidgetPanel()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QWidget#headerTEMP { background: #1A1A1A;}"
		"QWidget#Switcher { background: #1A1A1A; border-top: 1px solid #151515; border-bottom: 1px solid #151515; }"
		"QWidget#Switcher QPushButton { background-color: #333; padding: 4px 16px; }"
		"QWidget#DirControl { background: #1A1A1A; }"
		"QWidget#Switcher QPushButton:checked { background: #2980b9; }"
		"QWidget#BreadCrumb { background: #1A1A1A; border-top: 1px solid #151515; border-bottom: 1px solid #151515; }"
		"QWidget#BreadCrumb QPushButton { background: transparent; padding: 4px 16px;"
		"									border-right: 1px solid black; color: #999; }"
		"QWidget#BreadCrumb QPushButton:checked { color: white; border-right: 1px solid black; }"
		"QWidget#FilterWidget QPushButton:checked { color: white; background: #2980b9; }"
		"QWidget#assetTree { background: #202020; border: 0; }"
		"QWidget#assetView { background: #202020; border: 0; outline: 0; padding: 0; margin: 0; }"
		"QSplitter::handle { width: 1px; background: #151515; }"
		"QLineEdit { border: 0; background: #292929; color: #EEE; padding: 4px 8px; selection-background-color: #404040; color: #EEE; }"
		"QTreeView, QTreeWidget { show-decoration-selected: 1; }"
		"QWidget#assetView { alternate-background-color: #222; selection-background-color: transparent; }"
		"QListView::item:selected { background-color: #303030; }"
		"QListView::item:hover { background-color: #292929; }"
		"QTreeWidget { outline: none; selection-background-color: #404040; color: #EEE; }"
		"QTreeWidget::branch { background-color: #202020; }"
		"QTreeWidget::branch:hover { background-color: #303030; }"
		"QTreeView::branch:open { image: url(:/icons/expand_arrow_open.png); }"
		"QTreeView::branch:closed:has-children { image: url(:/icons/expand_arrow_closed.png); }"
		"QTreeWidget::branch:selected { background-color: #404040; }"
		"QTreeWidget::item:selected { selection-background-color: #404040;"
		"								background: #404040; outline: none; padding: 5px 0; }"
		/* Important, this is set for when the widget loses focus to fill the left gap */
		"QTreeWidget::item:selected:!active { background: #404040; padding: 5px 0; color: #EEE; }"
		"QTreeWidget::item:selected:active { background: #404040; padding: 5px 0; }"
		"QTreeWidget::item { padding: 5px 0; }"
		"QTreeWidget::item:hover { background: #303030; padding: 5px 0; }"
		"QPushButton{ background-color: #333; color: #DEDEDE; border : 0; padding: 4px 16px; }"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }"
		"QPushButton:disabled{ color: #444; }"
	);
}

const QString StyleSheet::AssetWidgetTagDialog()
{
	JAH_CLASSIC_ONLY
	return QString(
		"* { color: #EEE; }"
		"QDialog { background: #202020; padding: 4px; }"
		"QPushButton { background: #444; color: #EEE; border: 0; padding: 6px 10px; }"
		"QPushButton:hover { background: #555; color: #EEE; }"
		"QPushButton:pressed { background: #333; color: #EEE; }"
		"QListWidget { show-decoration-selected: 1; background: #202020; border: 0; outline: 0 }"
		"QListWidget::item:selected { background-color: #191919; }"
		"QListWidget::item:selected:active { background-color: #191919; }"
		"QListWidget::item { padding: 5px 0; }"
		"QListWidget::item:hover { background: #303030; }"
		"QListWidget::item:disabled { background: #202020; color: #888; }"
		"QListWidget::item:disabled:hover { background: #202020; color: #888; }"
		"QListWidget::item:hover:!active { background: #202020; color: #888; }"
		"QListWidget { spacing: 0 5px; }"
		"QListWidget::indicator { width: 18px; height: 18px; }"
		"QListWidget::indicator::unchecked { image: url(:/icons/check-unchecked.png); }"
		"QListWidget::indicator::checked { image: url(:/icons/check-checked.png); }"
		"QListWidget::indicator::disabled { image: url(:/icons/check-disabled.png); }"
	);
}

const QString StyleSheet::AssetViewCollectionDialog()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QLineEdit { font-size: 14px; background: #2f2f2f; padding: 6px; border: 0; }"
		                        "QPushButton { background: #4898ff; color: white; border: 0; padding: 8px 12px; border-radius: 1px; }"
		                        "QPushButton:hover { background: #51a1d6; }"
		                        "QDialog { background: #1a1a1a; }"
	);
}

const QString StyleSheet::AssetViewSearchField()
{
	JAH_CLASSIC_ONLY
	return QString(
		"border: 1px solid #1E1E1E; border-radius: 1px; "
		"font-size: 12px; background: #3B3B3B; padding: 6px 4px;"
	);
}

const QString StyleSheet::AssetViewFilterPane()
{
	JAH_CLASSIC_ONLY
	return QString(
		"#filterPane { background: #1E1E1E; border-bottom: 1px solid #111; }"
		"QLabel { font-size: 12px; margin-right: 8px; }"
		"QPushButton[accessibleName=\"filterObj\"] { border-radius: 0; padding: 10px 8px; }"
		"QComboBox { background: #222; border-radius: 1px; color: #BBB; padding: 0 12px; min-height: 30px; min-width: 72px; border: 1px solid #111;}"
		"QComboBox::drop-down { border: 0; margin: 0; padding: 0; min-height: 20px; }"
		"QComboBox::down-arrow { image: url(:/icons/down_arrow_check.png); width: 18px; height: 14px; }"
		"QComboBox::down-arrow:!enabled { image: url(:/icons/down_arrow_check_disabled.png); width: 18px; height: 14px; }"
		"QComboBox QAbstractItemView::item { min-height: 24px; selection-background-color: #404040; color: #cecece; }"
		"QComboBox QAbstractItemView { background-color: #1A1A1A; selection-background-color: #404040; border: 0; outline: none; }"
		"QComboBox QAbstractItemView::item:selected { background: #404040; }"
	);
}

const QString StyleSheet::AssetViewImportButtons()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QPushButton { background: #111; border: 1px solid black; border-radius: 1px; padding: 8px 12px; }"
	);
}

const QString StyleSheet::AssetViewAddToProjectButton()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QPushButton { background: #3498db; }"
		"QPushButton:hover { background-color: #4aa3de; }"
		"QPushButton:pressed { background-color: #1f80c1; }"
		"QPushButton:disabled { color: #656565; background-color: #3e3e3e; }"
	);
}

const QString StyleSheet::AssetViewDeleteButton()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QPushButton { background: #E74C3C } QPushButton:disabled { color: #656565; background-color: #3e3e3e; }"
	);
}

const QString StyleSheet::AssetViewChangeCollectionLink()
{
	JAH_CLASSIC_ONLY
	return QString(
		"font-size: 8px; color: green; padding: 0; background: transparent; border: 0"
	);
}

const QString StyleSheet::AssetViewMetadataHeader()
{
	JAH_CLASSIC_ONLY
	return QString(
		"border-top: 1px solid black; border-bottom: 1px solid black; text-align: center; padding: 12px; background: #1A1A1A"
	);
}

const QString StyleSheet::AssetViewPanel()
{
	JAH_CLASSIC_ONLY
	return QString(
		"*							{ color: #EEE; }"
		"QPushButton				{ background: #404040; border-radius: 2px; padding: 8px 12px; }"
		"QSplitter					{ background: #2E2E2E; } QSplitter:handle { background: black; }"
		"#localAssetsButton			{ text-align: left; padding: 12px; }"
		"#onlineAssetsButton		{ text-align: left; padding: 12px; }"
		"QPushButton[accessibleName=\"assetsButton\"]:disabled { color: #444; }"
		"#assetDropPad				{}"
		"#assetDropPadLabel			{ border: 4px dashed #111; border-radius: 4px; "
		"							  padding: 48px 36px; margin: 0; }"
		"#assetDropPad, #MetadataPane QPushButton	{ padding: 8px 12px; }"
		"QLineEdit					{ border: 1px solid #1E1E1E; border-radius: 2px; background: #3B3B3B; }"
		"#assetDropPad QLabel		{}"
		"QTreeView, QTreeWidget { show-decoration-selected: 1; border: 0; alternate-background-color: #252525;"
		"                         selection-background-color: #404040; color: #EEE; background: #202020;"
		"                         paint-alternating-row-colors-for-empty-area: 1; outline: none; }"
		//"QTreeWidget::branch { background-color: #202020; }"
		"QTreeView::branch:open { image: url(:/icons/expand_arrow_open.png); }"
		"QTreeView::branch:closed:has-children { image: url(:/icons/expand_arrow_closed.png); }"
		"QTreeWidget::branch:hover { background-color: #303030; }"
		"QTreeWidget::branch:selected { background-color: #404040; }"
		"QTreeWidget::item:selected { selection-background-color: #404040;"
		"							  background: #404040; outline: none; padding: 5px 0; }"
		/* Important, this is set for when the widget loses focus to fill the left gap */
		"QTreeWidget::item:selected:!active { background: #404040; padding: 5px 0; color: #EEE; }"
		"QTreeWidget::item:selected:active { background: #404040; padding: 5px 0; }"
		"QTreeWidget::item { padding: 5px 0; }"
		"QTreeWidget::item:hover { background: #303030; padding: 5px 0; }"
		"QComboBox { background: #1A1A1A; border : 0; }"
	);
}

const QString StyleSheet::AssetViewRenameDialog()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QLineEdit { font-size: 14px; background: #2f2f2f; padding: 6px; border: 0; }"
				"QComboBox { background: #4D4D4D; color: #BBB; padding: 6px; border: 0; }"
				"QComboBox::drop-down { border : 0; }"
				"QComboBox::down-arrow { image: url(:/icons/down_arrow_check.png); width: 18px; height: 14px; }"
				"QComboBox::down-arrow:!enabled { image: url(:/icons/down_arrow_check_disabled.png); width: 18px; height: 14px; }"
				"QPushButton { background: #4898ff; color: white; border: 0; padding: 8px 12px; border-radius: 1px; }"
				"QPushButton:hover { background: #555; }"
				"QDialog { background: #1A1A1A; }"
	);
}

const QString StyleSheet::SceneHierarchyTree()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QTreeView, QTreeWidget { show-decoration-selected: 1; paint-alternating-row-colors-for-empty-area: 1; }"
		"QTreeWidget { outline: none; selection-background-color: #404040; color: #EEE; }"
		//"QTreeWidget::branch { background-color: #202020; }"
		"QTreeWidget::branch:hover { background-color: #303030; }"
		"QTreeView::branch:open { image: url(:/icons/expand_arrow_open.png); }"
		"QTreeView::branch:closed:has-children { image: url(:/icons/expand_arrow_closed.png); }"
		"QTreeWidget::branch:selected { background-color: #404040; }"
		"QTreeWidget::item:selected { selection-background-color: #404040; background: #404040; outline: none; padding: 5px 0; }"
		"QTreeView, QTreeWidget { show-decoration-selected: 1; border: 0; outline: none; selection-background-color: #404040; color: #EEE; background: #202020; alternate-background-color: #222; }"
		/* Important, this is set for when the widget loses focus to fill the left gap */
		"QTreeWidget::item:selected:!active { background: #404040; padding: 5px 0; color: #EEE; }"
		"QTreeWidget::item:selected:active { background: #404040; padding: 5px 0; }"
		"QTreeWidget::item { padding: 5px 0; }"
		"QTreeWidget QLineEdit { background-color: #404040; selection-background-color: #777; border: 0; }"
		"QTreeWidget::item:hover { background: #303030; padding: 5px 0; }"
	);
}

const QString StyleSheet::ColorViewPanel()
{
	JAH_CLASSIC_ONLY
	return QString(
		"background: rgba(25,25,25,1);;"
	);
}

const QString StyleSheet::ColorViewInputCircle()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QWidget{background: rgba(20,20,20,1);}"
	);
}

const QString StyleSheet::ValueSliderGradient()
{
	JAH_CLASSIC_ONLY
	return QString(
		              QString(
		"QSlider::groove:horizontal {background: qlineargradient(x1: 1, y1: 0, x2: 0, y2: 0,stop: 0 white, stop: 1 black); border-radius: 1px; border: 1px solid rgba(0,0,0,1); }"
		" QSlider::handle:horizontal {height: 3px; width:14px; margin: -2px 0px; background: rgba(50,148,213,1); }"
		" QSlider::add-page:horizontal, QSlider::sub-page:horizontal {background: rgba(0,0,0,0); border-radius: 1px;}"

		)
	);
}

// The Preferences dialog's tab container. The dialog's own sheet paints the
// QDialog dark, but a QTabWidget draws a platform-light pane and tab bar over
// it, and unstyled labels inside the pages render black — the "white settings
// dialog" regression. One sheet on the QTabWidget covers the bar, the pane,
// every page background, and text defaults; widgets that carry their own
// styles (grouped buttons, styled line edits) still win, being closer.
const QString StyleSheet::PreferencesTabs()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QTabWidget::pane { border: 0; background: #222; }"
		"QStackedWidget { background: #222; }"
		"QTabBar { background: #222; }"
		"QTabBar::tab { background: #2b2b2b; color: #DEDEDE; border: 0; padding: 8px 18px; }"
		"QTabBar::tab:selected { background: rgba(50,148,213,0.9); color: #FFF; }"
		"QTabBar::tab:hover:!selected { background: #444; }"
		"QLabel { color: rgba(255,255,255,.9); background: transparent; }"
		"QCheckBox { color: rgba(255,255,255,.9); background: transparent; }"
		"QLineEdit { color: rgba(255,255,255,.9); background: rgba(64,64,64,1); border: 0; padding: 5px; selection-background-color: #808080; }"
		"QScrollArea { background: transparent; border: 0; }"
		"QScrollArea > QWidget > QWidget { background: transparent; }"
	);
}

const QString StyleSheet::MutedInfoText()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QLabel { color: rgba(255,255,255,.55); background: transparent; font-size: 11px; }"
	);
}

const QString StyleSheet::TopMenuDisabled()
{
	JAH_CLASSIC_ONLY
	return QString(
		"color: #444; border-color: #111"
	);
}

const QString StyleSheet::BackgroundTransparent()
{
	JAH_CLASSIC_ONLY
	return QString(
		"background: transparent"
	);
}

const QString StyleSheet::HelpButton()
{
	JAH_CLASSIC_ONLY
	return QString(
		"#helpButton { qproperty-icon: url(\"\");"
		"qproperty-iconSize: 48px 48px;"
		"background: transparent;"
		"color: rgba(255,255,255,.9);"
		"background-repeat: no-repeat; }"
		"#helpButton::hover {color: rgba(255, 255, 255, 1); }"
	);
}

const QString StyleSheet::PrefsButton()
{
	JAH_CLASSIC_ONLY
	return QString(
		"#prefsButton { qproperty-icon: url(\"\");"
		"qproperty-iconSize: 48px 48px;"
		"background: transparent;"
		"color: rgba(255,255,255,.9);"
		"background-repeat: no-repeat; }"
		"#prefsButton::hover { color: rgba(255, 255, 255, 1); }"
	);
}

const QString StyleSheet::ControlBar()
{
	JAH_CLASSIC_ONLY
	return QString(
		"#controlBar {  background: #1E1E1E; border-bottom: 1px solid black; }"
	);
}

const QString StyleSheet::DockToggleDialog()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QDialog { border: 1px solid black; background: #1E1E1E; }"
		"QPushButton { padding: 8px 24px; border-radius: 1px; }"
		"QPushButton[accessibleName=\"toggleAbles\"]:checked { background: #1E1E1E; }"
		"QPushButton[accessibleName=\"toggleAbles\"] { background: #3E3E3E; }"
	);
}

const QString StyleSheet::ItemGridTileButton()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QPushButton { background: transparent; font-weight: bold; color: white }"
		                              "QToolTip { padding: 2px; }"
	);
}

const QString StyleSheet::ItemGridTileControls()
{
	JAH_CLASSIC_ONLY
	return QString(
		"#fresh { background: rgba(32, 32, 32, 190); border-radius: 4px; }"
		                            "QLabel { font-weight: bold; font-size: 12px }"
	);
}

const QString StyleSheet::ItemGridTileCaptionActive()
{
	JAH_CLASSIC_ONLY
	return QString(
		"color: white"
	);
}

const QString StyleSheet::ItemGridTileCaptionIdle()
{
	JAH_CLASSIC_ONLY
	return QString(
		"color: rgba(255, 255, 255, 50%)"
	);
}

const QString StyleSheet::ProjectManagerCanvas()
{
	JAH_CLASSIC_ONLY
	return QString(
		"border: none;"
		"background-image: url(:/images/empty_canvas.png);"
		"background-attachment: fixed;"
		"background-position: center;"
		"background-origin: content;"
		"background-repeat: no-repeat;"
	);
}

const QString StyleSheet::ProjectManagerSampleList()
{
	JAH_CLASSIC_ONLY
	return QString(
		"#sampleList { background-color: #1e1e1e; padding: 0 8px; border: none; color: #EEE; } " \
		                              "QListWidgetItem { padding: 12px; } "\
		                              "QListView::item:selected { "\
		                              "    border: 1px solid #3498db; "\
		                               " background: #3498db; "\
		                               "  color: #CECECE; "\
		                              "} "\
		                              "*, QLabel { color: white; } "\
		                              "QToolTip { padding: 2px; border: 0; background: black; opacity: 200; }"
	);
}

const QString StyleSheet::ProjectManagerInstructions()
{
	JAH_CLASSIC_ONLY
	return QString(
		"#instructions { border: none; background: #1e1e1e; color: white; " \
		                                "padding: 10px; font-size: 12px }"
	);
}

const QString StyleSheet::QLineEditDisabled()
{
	JAH_CLASSIC_ONLY
	return QString(
		"background: #303030; color: #888;"
	);
}

const QString StyleSheet::QMenuDarkDesktop()
{
	JAH_CLASSIC_ONLY
	return QString(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 16px 6px 10px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; }"
		"QMenu::item:disabled { color: #555; }"
	);
}

const QString StyleSheet::TopMenuSelected()
{
	JAH_CLASSIC_ONLY
	return QString(
		"border-color: #3498db"
	);
}

const QString StyleSheet::TopMenuUnselected()
{
	JAH_CLASSIC_ONLY
	return QString(
		"border-color: #111"
	);
}

const QString StyleSheet::MainWindowHeaderLogo(const QString &imagePath)
{
	JAH_CLASSIC_ONLY
	return QString("image: url(%1);").arg(imagePath);
}

const QString StyleSheet::AssetGridItemLabel(const QString &borderColor)
{
	JAH_CLASSIC_ONLY
	return QString("color: #ddd; font-size: 12px; background: #1e1e1e;")
		+ "border-left: 3px solid " + borderColor
		+ "; border-bottom: 3px solid " + borderColor
		+ "; border-right: 3px solid " + borderColor;
}

const QString StyleSheet::AssetGridItemThumbnail(const QString &borderColor)
{
	JAH_CLASSIC_ONLY
	return QString("border-left: 3px solid ") + borderColor
		+ "; border-top: 3px solid " + borderColor
		+ "; border-right: 3px solid " + borderColor;
}

const QString StyleSheet::ItemGridTileBorder(int width)
{
	JAH_CLASSIC_ONLY
	return QString("border: ") + QString::number(width) + "px solid rgba(0, 0, 0, 10%)";
}

const QString StyleSheet::ItemGridTileBorderHighlight(int width)
{
	JAH_CLASSIC_ONLY
	return QString("border: ") + QString::number(width) + "px dashed #3498db";
}

const QString StyleSheet::ItemGridTileLabel(int fontSize)
{
	JAH_CLASSIC_ONLY
	return QString("color: #ddd; font-size: ") + QString::number(fontSize) + "px;";
}

void StyleSheet::setStyle(QWidget *widget)
{
	auto name = widget->metaObject()->className();

	if (name == QStringLiteral("QPushButton")) widget->setStyleSheet(QPushButtonGreyscale());
	if (name == QStringLiteral( "QLineEdit")) widget->setStyleSheet(QLineEdit());
	if (name == QStringLiteral( "QLabel")) widget->setStyleSheet(QLabelWhite());
	if (name == QStringLiteral( "QComboBox")) widget->setStyleSheet(QComboBox());
	if (name == QStringLiteral( "QCheckBox")) widget->setStyleSheet(QCheckBox());
	if (name == QStringLiteral( "QSpinBox") || name == QStringLiteral("QDoubleSpinBox")) widget->setStyleSheet(QSpinBox());
	if (name == QStringLiteral( "QSplitter")) widget->setStyleSheet(QSpinBox());
	if (name == QStringLiteral( "QTextBrowser")) widget->setStyleSheet(QAbstractScrollArea());
}

void StyleSheet::setStyle(QObject *obj)
{
	auto name = obj->metaObject()->className();
	if (name == QStringLiteral("QButtonGroup")) {
		auto bg = static_cast<QButtonGroup*>(obj);
		for (auto btn : bg->buttons()) {
			btn->setCheckable(true);
			btn->setStyleSheet(QPushButtonGrouped());
		}
	}
}

void StyleSheet::setStyle(QList<QWidget*> list)
{
	for (auto wid : list) setStyle(wid);
}










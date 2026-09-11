/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// THE THEME SWEEP'S CLASSIC ARCHIVE (lane 16). The archived "Jahshaka Classic"
// theme was assembled from hundreds of raw setStyleSheet() strings and
// .ui-embedded styleSheet properties; under the default Qlementine theme each
// of them interposed QStyleSheetStyle over the QStyle (THEME_AUDIT.md §3 — the
// dark-on-dark and platform-light hybrids). The sweep moved every one Classic
// still needs HERE, verbatim, behind the same kill switch as stylesheet.cpp:
//
//   Classic     -> the getter returns exactly the CSS its call site used to
//                  pass (or the .ui used to embed), so the same sheet lands on
//                  the same widget and Classic renders bit-for-bit;
//   Qlementine  -> "" — an empty sheet never interposes QStyleSheetStyle.
//
// Qlementine's side of each site lives at the call site: nothing (the style
// already draws it), a palette/font role (ui/style/themeroles.h), or — for the
// few looks a QStyle cannot express — a ThemeManager sheet.
//
// When Classic is retired this file is deleted whole, with stylesheet.cpp.

#include "ui/style/stylesheet.h"

#include <QColor>

#define JAH_CLASSIC_ONLY if (!StyleSheet::classicThemeActive()) return QString();

const QString StyleSheet::MainWindowRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QToolButton
{
}

QToolButton:disabled
{
}

QToolButton:pressed
{
}

QToolButton:focus
{
}

QToolButton:hover
{
}

QToolButton:checked
{
    background-color: #2980b9;
}

#header {
	background: #111;
	border-bottom: 1px solid black;
}

#worlds_menu, #player_menu, #editor_menu, #assets_menu, #effects_menu, #publish_menu, #avatar_menu {
	font-size: 17px;
	padding: 14px;
	border-bottom-width: 4px;
	border-bottom-style: solid;
	border-color: #111;
	border-radius: 0px;
	background: transparent;
}

/* ====== TABS ====== */
/* https://stackoverflow.com/a/33006868/996468 */
QTabBar {
	background-color: #151515;
	qproperty-drawBase: 0;
	/* border-top: 1px solid #111; */
}

QTabWidget::tab-bar {
    left: 0;
}

QTabBar::close-button {

}

QTabWidget::pane {

}

QTabBar::tab:top {
    background-color: #181818;
    border-top: 2px solid #151515;
    padding: 8px 14px;
    color: #777;
}

QTabBar::tab:top:selected {
	background-color: #202020;
	border-top: 1px solid #3498db;
    color: #EEE;
}

QTabBar::tab:bottom {
    background-color: #181818;
    border-bottom: 2px solid #151515;
    padding: 8px 14px;
    color: #777;
}

QTabBar::tab:bottom:selected {
	background-color: #202020;
	border-bottom: 1px solid #3498db;
    color: #EEE;
}

/* ========================== */

QCheckBox {
    spacing: 0 5px;
}

QCheckBox::indicator {
    width: 18px;
    height: 18px;
}

QCheckBox::indicator::unchecked {
	image: url(:/icons/check-unchecked.png);
}

QCheckBox::indicator::checked {	
	image: url(:/icons/check-checked.png);
}

/* ========================== */

QScrollBar:vertical {
	width: 12px;
	border-left: 1px solid black;
    background: transparent;
}

QScrollBar::handle:vertical {
    background: #5A5F66;
	border: 1px solid black;
	min-height: 32px;
	margin: 2px 1px 2px 1px;
	border-radius: 4px; /* this appears to be tied to the (width of the scrollbar / 2) */
}

QScrollBar::handle:vertical:hover {
    background-color: #6b6f77;
}

QScrollBar::add-page:vertical,  QScrollBar::sub-page:vertical {
    background: #17181a;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
      border: none;
      background: none;
}

/* ========================== */



/* ========================== */

QMainWindow {
    background-color: #151515;
}

QMainWindow::separator {
    width: 1px;
    height: 0px;
    margin: 0px;
    padding: 0px;
	background: black;
}

* {
    color: #EEE;
}

QDialog {
    background: #1A1A1A;
}

QMessageBox {
    background: #1A1A1A;
}

QMessageBox QPushButton  {
    border: none;
    background-color: #212121;
    padding: 4px 12px;
}

QMessageBox QPushButton:hover {
    background-color: #555;
}

/* ==== DOCKS === */

QDockWidget::title {
    background-color: #1E1E1E;
    text-align: center;
    padding: 9px;
	border-bottom: 1px solid black;
}

QDockWidget {
    border: 0;
    titlebar-close-icon: url(:/icons/close.png);
    titlebar-normal-icon: url(:/icons/popout.png);
}

QDockWidget::close-button, QDockWidget::float-button {
    padding: 0px;
	border: 0px;
    icon-size: 14px;
}

QDockWidget::close-button {
    subcontrol-position: top right;
    subcontrol-origin: margin;
    position: absolute;
    top: 0px; right: 0px; bottom: 0px;
    width: 14px;
}

QDockWidget::float-button {
    subcontrol-position: top right;
    subcontrol-origin: margin;
    position: absolute;
    top: 0px; right: 14px; bottom: 0px;
    width: 14px;
}

QDockWidget::close-button:hover, QDockWidget::float-button:hover {
	background: transparent;
    icon-size: 14px;
	border: 0;
}

/* ==== VR === */

QPushButton#vrButton {
    border: none;
    background-color: rgba(255, 255, 255, 0);
}

QPushButton#vrButton[vrMode="1"] {
    background-color: #e74c3c;
}

QPushButton#vrButton[vrMode="2"] {
    background-color: #2980b9;
}

QPushButton#vrButton:hover {
    background-color: #3498db;
}

/* ====== Labels, Buttons ====== */
QPushButton, QToolButton  {
    border: none;
    border-radius: 1px;
    background-color: #333;
}

QPushButton:hover, QToolButton:hover {
    background-color: #444;
}

QToolTip {
    padding: 2px;
    border: 0;
    background: black;
    opacity: 200;
}

QLabel {
    color: #BBB;
}

/* ====== TOOLBAR ====== */
QToolBar > QToolButton, QWidget#vrButton, #pmButton {
    background-color: #212121;
    border: 1px solid #1e1e1e;
    border-radius: 2px;
    padding: 8px;
    width: 14px;
    height: 14px;
    margin: 8px;
}

QPushButton#pmButton:hover {
    background-color: #3498db;
}

QToolBar {
    background:  #303030;
   /* spacing: 4px; /* spacing between items in the tool bar */
    /*min-height: 21px;*/
    border-bottom: 1px solid black;
}

QToolBar::handle:horizontal {
    image: url(:/icons/thandleh.png);
	width: 24px;
}

QToolBar::handle:vertical {
    image: url(:/icons/thandlev.png);
	height: 24px;
}

QToolBar::separator:horizontal {
    background-color: #272727;
    width: 1px;
    margin-left: 6px;
    margin-right: 6px;
}

/* special override */
QWidget#addBtn, QWidget#deleteBtn {
    background-color: #4D4D4D;
}

QWidget#addBtn:hover, QWidget#deleteBtn:hover {
    background-color: #555;
}

QWidget#addBtn:pressed, QWidget#deleteBtn:pressed {
    background-color: #444;
}

QMessageBox {
    background: #222;
}

)CSS");
}

const QString StyleSheet::MainWindowPropertiesDock()
{
	JAH_CLASSIC_ONLY
	return QString("QWidget { background-color: #202020; }");
}

const QString StyleSheet::BorderNone()
{
	JAH_CLASSIC_ONLY
	return QString("border: 0");
}

const QString StyleSheet::MainWindowPresetsDock()
{
	JAH_CLASSIC_ONLY
	return QString("QWidget { background-color: #151515; }");
}

const QString StyleSheet::ViewportMenuButton()
{
	JAH_CLASSIC_ONLY
	return QString("padding: 0 8px 0 0; margin: 0");
}

const QString StyleSheet::ViewportCameraToggle()
{
	JAH_CLASSIC_ONLY
	return QString("QPushButton{background:rgba(0,0,0,0);}");
}

const QString StyleSheet::PlayerControlsBar()
{
	JAH_CLASSIC_ONLY
	return QString("background: #1A1A1A");
}

const QString StyleSheet::AccordionBladeRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QWidget#AccordionBladeWidget {
	background-color: #212121;
	border: 0;
}

QWidget#bg {
	background-color: #4D4D4D;
}

QPushButton#toggle {
	background-color: #4D4D4D;
    border: 0;
}

QLabel#content_title {
	background-color: #4D4D4D;
})CSS");
}

const QString StyleSheet::HFloatSliderRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QWidget#floater {
	/*background: #212121; */
}

QSlider::sub-page {
	border: 0px solid transparent;
	height: 2px;
	background: #3498db;
	margin: 2px 0;
}


QSlider::groove:horizontal {
    border: 0px solid transparent;
    height: 4px;
    background: #1e1e1e;
    margin: 2px 0;
}

QSlider::handle:horizontal {
    background-color: #CCC;
    width: 12px;
    border: 1px solid #1e1e1e;
    margin: -5px 0px;
    border-radius:7px;
}

QSlider::handle:horizontal:pressed {
    background-color: #AAA;
    width: 12px;
    border: 1px solid #1e1e1e;
    margin: -5px 0px;
    border-radius: 7px;
}

QDoubleSpinBox {
	border-radius: 1px;
	padding: 7px;
	background: #292929;
}

QSlider::handle:horizontal:disabled {
    background-color: #bbbbbb;
    width: 12px;
    border: 0px solid transparent;
    margin: -1px -1px;
    border-radius: 4px;
})CSS");
}

const QString StyleSheet::ComboBoxWidgetRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(
   QComboBox {
    /*border-radius: 3px;
    padding: 1px 18px 1px 3px;
    min-width: 6em;*/
}

QComboBox:editable {

}

QComboBox QAbstractItemView::item {
    show-decoration-selected: 1;
}

QComboBox QAbstractItemView::item {
    padding: 6px;
}

QListView::item:selected {
    background: #404040;
}

QComboBox:!editable, QComboBox::drop-down:editable {
     background: #1A1A1A;
}

/* QComboBox gets the on state when the popup is open */
QComboBox:!editable:on, QComboBox::drop-down:editable:on {
    background: #1A1A1A;
}

QComboBox QAbstractItemView {
    background-color: #1A1A1A;
    selection-background-color: #404040;
    border: 0;
    outline: none;
}

QComboBox QAbstractItemView::item {
    border: none;
    padding-left: 5px;
}

QComboBox QAbstractItemView::item:selected {
    background: #404040;
    padding-left: 5px;
}

QComboBox QAbstractItemView::item:!enabled {
    background: #1A1A1A;
    color: #555;
    padding-left: 5px;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 18px;
    border-left-width: 1px;
}

QComboBox::down-arrow {
    image: url(:/icons/down_arrow_check.png);
	width: 18px;
	height: 14px;
}

QComboBox::down-arrow:!enabled {
    image: url(:/icons/down_arrow_check_disabled.png);
    width: 18px;
    height: 14px;
}
)CSS");
}

const QString StyleSheet::TexturePickerRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QPushButton {
    background-color: #4D4D4D;
    color: #DEDEDE;
	padding: 6px;
    border: 0;
}

QPushButton:hover {
    background-color: #555;
}

QPushButton:pressed {
    background-color: #444;
}

QWidget#texture {
	background-color: #4D4D4D;
})CSS");
}

const QString StyleSheet::FilePickerRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QPushButton {
    background-color: #4D4D4D;
    color: #DEDEDE;
    border: 0;
}

QPushButton:hover {
    background-color: #555;
}

QPushButton:pressed {
    background-color: #444;
})CSS");
}

const QString StyleSheet::FilePickerFilename()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(background: #404040; padding: 2px)CSS");
}

const QString StyleSheet::AssetPickerRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QWidget#AssetPickerWidget, #assetView {
	background: #202020;
}

QLabel {
	color: #EEE;
}

QPushButton {
    background-color: #4D4D4D;
    color: #DEDEDE;
    border: 0;
	padding: 2px 8px;
}

QPushButton:hover {
    background-color: #555;
}

QPushButton:pressed {
    background-color: #444;
}

QSlider::groove:horizontal {
   /* height: 8px; /* the groove expands to the size of the slider by default. by giving it a height, it has a fixed size */
    background: #404040;
}

QSlider::handle:horizontal {
    background: #AFAFAF;
    width: 10px;
    margin: -2px 0; /* handle is placed by default on the contents rect of the groove. Expand outside the groove */
}

QListView {
	background: #303030;
	border: 0;
}

QListWidget {
	background: #404040;
	color: #CECECE;
}

QListView::item:selected {
    border: 1px solid #3498db;
	background: #3498db;
	color: #CECECE;
}

/* ================== */

 QScrollBar:vertical {
     background-color: #212121;
     width: 18px;
     margin: 22px 0 22px 0;
 }

 QScrollBar::handle:vertical {
	background-color: #444;
     min-height: 20px;
     margin-left: 2px;
     margin-right: 2px;
 }

QScrollBar::handle:vertical:hover {
	background-color: #555;
	min-height: 20px;
	margin-left: 2px;
	margin-right: 2px;
}

QScrollBar::add-line:vertical {
	background-color: #444;
	height: 20px;
	border: 2px solid #212121;
	border-width: 2px 2px  0 2px;
	subcontrol-position: bottom;
	subcontrol-origin: margin;
 }

QScrollBar::sub-line:vertical {
    background-color: #444;
	height: 20px;
	border: 2px solid #212121;
	border-width: 0 2px 2px 2px;
	subcontrol-position: top;
	subcontrol-origin: margin;
 }

QScrollBar::up-arrow:vertical {
    image: url(:/icons/up-arrow.svg);
}

QScrollBar::up-arrow:vertical:hover {
    background-color: #555;
}

QScrollBar::down-arrow:vertical {
    image: url(:/icons/down-arrow.svg);
}

QScrollBar::down-arrow:vertical:hover {
    background-color: #555;
}

/* ========================== */

/* For icon only */
/*QListView::icon {
    left: 10px;
}*/

/* For text only */
/*QListView::text {
    left: 10px;
}*/)CSS");
}

const QString StyleSheet::AssetPickerAssetView()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QScrollBar:vertical {
	background: #1e1e1e;
})CSS");
}

const QString StyleSheet::SceneHierarchyRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QWidget#sceneTree {
	background-color: #202020;
	border-top: 1px solid #111;
}

#addBtn, #deleteBtn, #folderBtn {
	padding: 4px;
}

#addBtn {
	border: 0;
}

#deleteBtn, #folderBtn {
	border-left: 1px solid #333;
}

QTreeWidget {
  outline: none;
  selection-background-color: #404040;
  color: #CECECE;
}

QTreeWidget::item {
	padding: 6px;
}

QTreeWidget::item:selected {
	selection-background-color: #404040;
	background: #404040;
	outline: none;
  }


/* important when the widget loses focus */
QTreeWidget::item:selected:!active {
	background: #404040;
	padding: 0;
	color: #CECECE;
}

QTreeWidget::item:selected:active {
	background: #404040;
	padding: 0;
}

QTreeWidget::item:hover {

}
  )CSS");
}

const QString StyleSheet::SceneHierarchyWidget()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QWidget#widget{
	border: none;
	background-color: #303030;
})CSS");
}

const QString StyleSheet::SceneHierarchySceneTree()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(outline: none)CSS");
}

const QString StyleSheet::SkyPresetsRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QListWidget {
	padding: 4px;
  border: 0;
	background: #202020;
})CSS");
}

const QString StyleSheet::AnimationWidgetRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QWidget#AnimationWidget {
	background: #202020;
}

QPushButton:!enabled, QToolButton:!enabled, QComboBox:!enabled {
	background: #222;
	color: #555;
}

QSplitter::handle {
	background: black;
}

QPushButton {
    background-color: #4D4D4D;
    color: #DEDEDE;
    border: 0;
	padding: 4px 16px;
}

QPushButton:hover {
    background-color: #555;
}

QPushButton:pressed {
    background-color: #444;
}

#widget_11 > QPushButton {
	padding: 4px;
	background: transparent;
}

#insertFrame {
	padding-right: 4px;
}
/*
#insertFrame {
    background-color: #4D4D4D;
    color: #DEDEDE;
    border: 0;
	padding: 2px 8px;
}

#insertFrame:hover {
    background-color: #555;
}

#insertFrame:pressed {
    background-color: #444;
}
*/

#curvesBtn, #dopeSheetBtn {
	width: 64px;
}

#widget_11 > #stopBtn {
	height: 10px;
	width: 12px;
	margin-left: -2px;
	margin-right: 5px;
	background: transparent;
}

QWidget#controls > QToolButton:hover {
	background-color: rgb(235, 235, 235);
	border-style: inset;
}

QComboBox {
    background: #1A1A1A;
    border: 0;
}

QComboBox:editable {

}

QComboBox QAbstractItemView::item {
    show-decoration-selected: 1;
}

QComboBox QAbstractItemView::item {
    padding: 6px;
}

QListView::item:selected {
    background: #404040;
}

QComboBox:!editable, QComboBox::drop-down:editable {
     background: #1A1A1A;
}

/* QComboBox gets the on state when the popup is open */
QComboBox:!editable:on, QComboBox::drop-down:editable:on {
    background: #1A1A1A;
}

QComboBox QAbstractItemView {
    background-color: #1A1A1A;
    selection-background-color: #404040;
    border: 0;
    outline: none;
}

QComboBox QAbstractItemView::item {
    border: none;
    padding-left: 5px;
}

QComboBox QAbstractItemView::item:selected {
    background: #404040;
    padding-left: 5px;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 18px;
    border-left-width: 1px;
}

QComboBox::down-arrow {
    image: url(:/icons/down_arrow_check.png);
	width: 18px;
	height: 14px;
}

QComboBox::down-arrow:!enabled {
    image: url(:/icons/down_arrow_check_disabled.png);
    width: 18px;
    height: 14px;
}

QTreeView, QTreeWidget { show-decoration-selected: 1; }
QTreeWidget { outline: none; selection-background-color: #404040; color: #EEE; background: #202020; border: 0; }
QTreeWidget::branch { background-color: #202020; }
QTreeWidget::branch:hover { background-color: #303030; }
QTreeWidget::branch:selected { background-color: #404040; }
QTreeWidget::item:selected { selection-background-color: #404040;
								background: #404040; outline: none; padding: 5px 0; }
/* Important, this is set for when the widget loses focus to fill the left gap */
QTreeWidget::item:selected:!active { background: #404040; padding: 5px 0; color: #EEE; }
QTreeWidget::item:selected:active { background: #404040; padding: 5px 0; }
QTreeWidget::item { padding: 5px 0; }
QTreeWidget::item:hover { background: #303030; padding: 5px 0; }

)CSS");
}

const QString StyleSheet::AnimationWidgetInsertFrame()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QComboBox {
    background: #1A1A1A;
    border: 0;
}

QComboBox:editable {

}

QComboBox QAbstractItemView::item {
    show-decoration-selected: 1;
}

QComboBox QAbstractItemView::item {
    padding: 6px;
}

QListView::item:selected {
    background: #404040;
}

QComboBox:!editable, QComboBox::drop-down:editable {
     background: #1A1A1A;
}

/* QComboBox gets the on state when the popup is open */
QComboBox:!editable:on, QComboBox::drop-down:editable:on {
    background: #1A1A1A;
}

QComboBox QAbstractItemView {
    background-color: #1A1A1A;
    selection-background-color: #404040;
    border: 0;
    outline: none;
}

QComboBox QAbstractItemView::item {
    border: none;
    padding-left: 5px;
}

QComboBox QAbstractItemView::item:selected {
    background: #404040;
    padding-left: 5px;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 18px;
    border-left-width: 1px;
}

QComboBox::down-arrow {
    image: url(:/icons/down_arrow_check.png);
	width: 18px;
	height: 14px;
}

QComboBox::down-arrow:!enabled {
    image: url(:/icons/down_arrow_check_disabled.png);
    width: 18px;
    height: 14px;
})CSS");
}

const QString StyleSheet::TimelineModeActive()
{
	JAH_CLASSIC_ONLY
	return QString("background: #2980b9");
}

const QString StyleSheet::TimelineModeIdle()
{
	JAH_CLASSIC_ONLY
	return QString("background: #555");
}

const QString StyleSheet::TransformEditorPanel()
{
	JAH_CLASSIC_ONLY
	return QString("QWidget#TransformEditor { border: none; }"
		"QLabel { color: #DEDEDE; background: transparent; }"
		"QDoubleSpinBox {"
		"    border-radius: 1px; padding: 3px; background: #292929; color: #DEDEDE;"
		"    selection-background-color: #3498db;"
		"}"
        // axis identity moved from the old X/Y/Z chips to a colored edge per field
		"DragSpinBox#xpos, DragSpinBox#xrot, DragSpinBox#xscale { border-left: 3px solid #c0392b; }"
		"DragSpinBox#ypos, DragSpinBox#yrot, DragSpinBox#yscale { border-left: 3px solid #27ae60; }"
		"DragSpinBox#zpos, DragSpinBox#zrot, DragSpinBox#zscale { border-left: 3px solid #2980b9; }"
		"QPushButton#resetBtn { background-color: #333; color: #DEDEDE; border: 0;"
		"                       padding: 4px 16px; border-radius: 1px; }"
		"QPushButton#resetBtn:hover { background-color: #555; }"
		"QPushButton#resetBtn:pressed { background-color: #444; }");
}

const QString StyleSheet::DragValueRowPanel()
{
	JAH_CLASSIC_ONLY
	return QString("QWidget#DragValueRow { border: none; background: transparent; }"
		"QLabel { color: #DEDEDE; background: transparent; }"
		"QDoubleSpinBox {"
		"    border-radius: 1px; padding: 3px; background: #292929; color: #DEDEDE;"
		"    selection-background-color: #3498db;"
		"}"
		"DragSpinBox#dragx { border-left: 3px solid #c0392b; }"
		"DragSpinBox#dragy { border-left: 3px solid #27ae60; }"
		"DragSpinBox#dragz { border-left: 3px solid #2980b9; }");
}

const QString StyleSheet::ParticleRampSwatch(const QColor &colour)
{
	JAH_CLASSIC_ONLY
	return QStringLiteral("background-color: %1; border: 1px solid #222;")
		.arg(colour.name());
}

const QString StyleSheet::ToolTipPopup()
{
	JAH_CLASSIC_ONLY
	return QString("#container{background:rgba(0,0,00,.1); border: 2px solid rgba(0,0,0,.5); border-radius: .1px; padding: 3px;}"
		"#header{background:rgba(50,50,50,.9); border: 0px solid rgba(0,0,0,.3); border-radius: 0px; padding: 5px 3px; color :rgba(255,255,255,.9);}"
		"#body{background:rgba(70,70,70,.9); border: 0px solid rgba(0,0,0,.3); border-radius: 0px; padding: 3px; margin:0px; color :rgba(255,255,255,.9);}");
}

const QString StyleSheet::ScriptConsolePanel()
{
	JAH_CLASSIC_ONLY
	return QString("#ScriptConsole { background-color: #151515; }"
		"QPlainTextEdit { background-color: #1a1a1a; color: #e6e6e6;"
		"  font-family: 'DejaVu Sans Mono', Consolas, monospace; font-size: 12px;"
		"  border: 1px solid #262626; }"
		"QPushButton { background-color: #2b2b2b; color: #e6e6e6; border: 1px solid #3a3a3a;"
		"  padding: 4px 10px; }"
		"QPushButton:hover { background-color: #3a3a3a; }");
}

const QString StyleSheet::WarningNote()
{
	JAH_CLASSIC_ONLY
	return QString(QStringLiteral("color: #d08b3c;"));
}

const QString StyleSheet::PresetsListPanel()
{
	JAH_CLASSIC_ONLY
	return QString("QListWidget { padding: 4px; border: 0; background: #202020; }");
}

const QString StyleSheet::PresetsContextMenu()
{
	JAH_CLASSIC_ONLY
	return QString("QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
		"QMenu::item : disabled { color: #555; }");
}

const QString StyleSheet::AssetViewMutedLabel()
{
	JAH_CLASSIC_ONLY
	return QString("color: #BABABA;");
}

const QString StyleSheet::AssetViewPreviewTitle()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 14px; color: #EEEEEE;");
}

const QString StyleSheet::AssetViewEmptyPreview()
{
	JAH_CLASSIC_ONLY
	return QString("background: #1e1e1e;");
}

const QString StyleSheet::AssetViewEmptyPreviewLabel()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 14px; color: #8f8f8f; background: transparent;");
}

const QString StyleSheet::AssetViewLocalAssetsLabel()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 12px; padding: 4px;");
}

const QString StyleSheet::AssetViewNavPane()
{
	JAH_CLASSIC_ONLY
	return QString("background: #202020;");
}

const QString StyleSheet::AssetViewEmptyLibraryLabel()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 16px; color: #BABABA;");
}

const QString StyleSheet::AssetViewPaneBorderless()
{
	JAH_CLASSIC_ONLY
	return QString("background: #202020; border: 0");
}

const QString StyleSheet::AssetViewTailStatus()
{
	JAH_CLASSIC_ONLY
	return QString("padding: 4px 10px; color: #9a9a9a; font-size: 12px;");
}

const QString StyleSheet::AssetViewPaneBackground()
{
	JAH_CLASSIC_ONLY
	return QString("background: #202020");
}

const QString StyleSheet::AssetViewUpdateButton()
{
	JAH_CLASSIC_ONLY
	return QString("background: #3498db");
}

const QString StyleSheet::AssetViewNothingSelected()
{
	JAH_CLASSIC_ONLY
	return QString("padding: 12px; text-align: center");
}

const QString StyleSheet::AssetViewStoreOfflineBanner()
{
	JAH_CLASSIC_ONLY
	return QString("#StoreOfflineBanner { background: #7a4a12; }"
		"#StoreOfflineBanner QLabel { color: #ffe0b3; background: transparent; }");
}

const QString StyleSheet::AssetGridTile()
{
	JAH_CLASSIC_ONLY
	return QString("background: #272727");
}

const QString StyleSheet::AssetGridLoadingOverlay()
{
	JAH_CLASSIC_ONLY
	return QString("background: rgba(0, 0, 0, 55%); color: #ffffff; font-size: 12px;");
}

const QString StyleSheet::AssetGridLoadingOverlayAccent()
{
	JAH_CLASSIC_ONLY
	return QString("background: rgba(0, 0, 0, 55%); color: #3498db; font-size: 12px;");
}

const QString StyleSheet::VideoPreviewTitle()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 14px; color: #EEEEEE; padding: 4px;");
}

const QString StyleSheet::ItemGridTileSpacer()
{
	JAH_CLASSIC_ONLY
	return QString("background: transparent; color: white");
}

const QString StyleSheet::ToastPanel()
{
	JAH_CLASSIC_ONLY
	return QString("QWidget#Toast { background: #1E1E1E; border: 1px solid #3498db; }"
		"QLabel { color: #EEE; }"
		"QLabel#Caption { font-style: bold; font-size: 16px; }");
}

const QString StyleSheet::ProjectManagerRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QMessageBox QPushButton  {
    border: none;
    background-color: #212121;
    padding: 4px 16px;
}

QMessageBox QPushButton:hover {
    background-color: #555;
}

#searchIcon {
	background: #303030;
	padding: 4px;
}

/* ========================== */

#lineEdit {
	font-size: 14px;
	background: #2f2f2f;
	padding: 3px;
	border: 0;
}

#newProject {
	background: #4898ff;
	color: white;
}

#newProject:hover {
	background-color: #51a1d6;
}

#samples, #projects {
	padding: 8px;
	color: white;
}

#listWidget, #listWidget_2 {
	background: #fff;
}

#listWidget {
	padding-top: 4px;
	padding-left: 2px;
}

QPushButton, QToolButton {
	border: 0;
	padding: 8px 12px;
	background: #444;
	border-radius: 4px;
}

QPushButton::hover {
	background: #555;
}

QToolButton::hover {
	background: #555;
}

QComboBox {
    background: #1A1A1A;
    border: 0;
}

QComboBox:editable {

}

QComboBox QAbstractItemView::item {
    show-decoration-selected: 1;
}

QComboBox QAbstractItemView::item {
    padding: 6px;
}

QListView::item:selected {
    background: #404040;
}

QComboBox:!editable, QComboBox::drop-down:editable {
     background: #1A1A1A;
}

/* QComboBox gets the on state when the popup is open */
QComboBox:!editable:on, QComboBox::drop-down:editable:on {
    background: #1A1A1A;
}

QComboBox QAbstractItemView {
    background-color: #1A1A1A;
    selection-background-color: #404040;
    border: 0;
    outline: none;
}

QComboBox QAbstractItemView::item {
    border: none;
    padding-left: 5px;
}

QComboBox QAbstractItemView::item:selected {
    background: #404040;
    padding-left: 5px;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 18px;
    border-left-width: 1px;
}

QComboBox::down-arrow {
    image: url(:/icons/down_arrow_check.png);
	width: 18px;
	height: 14px;
}

QComboBox::down-arrow:!enabled {
    image: url(:/icons/down_arrow_check_disabled.png);
    width: 18px;
    height: 14px;
}
)CSS");
}

const QString StyleSheet::AboutDialogRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(#AboutDialog {
	background: rgb(22, 26, 27);
}

QPushButton {
	border: 0;
	background: #444;
	padding: 8px 24px;
}

QPushButton:hover {
	background: #555;
}
)CSS");
}

const QString StyleSheet::AboutDialogTextBrowser()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(background: rgb(22, 26, 27))CSS");
}

const QString StyleSheet::DonateDialogRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(#Donate {
	border: 0 px solid black;
}

* {
	color: #EEE;
}


QCheckBox {
    spacing: 5px;
	font-size: 12px;
}

QCheckBox::indicator {
    width: 20px;
    height: 20px;
}

QCheckBox::indicator::unchecked {
	image: url(:/icons/check-unchecked.png);
}

QCheckBox::indicator::checked {	
	image: url(:/icons/check-checked.png);
}
)CSS");
}

const QString StyleSheet::DonateDialogBackground()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(#widget_background{
 border-image: url(:/images/splashv3.png);
border: 0px;
})CSS");
}

const QString StyleSheet::DonateDialogCtrl()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(#ctrl {
	background: #111;
}

QPushButton, QToolButton {
	border: 0;
	padding: 6px 24px;
	background: #444;
}

QPushButton::hover {
	background: #555;
}

QToolButton::hover {
	background: #555;
})CSS");
}

const QString StyleSheet::ProgressDialogRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QLabel {
	color: #EEEEEE;
}

QProgressBar {
     border: 1px solid black;
     background-color: #DEDEDE;
 }

 QProgressBar::chunk {
     background-color: #3498db;
 }

#ProgressDialog {
	background: #222;
	border: 1px solid black;
})CSS");
}

const QString StyleSheet::ProgressDialogStageLabel()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(color: #999999;)CSS");
}

const QString StyleSheet::RenameProjectDialogRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(* {
	color: #EEE;
}

#RenameProjectDialog {
	background: #1A1A1A;
}

QPushButton  {
    border: none;
    background-color: #212121;
    padding: 6px 16px;
}

QPushButton:hover {
    background-color: #555;
}

QLabel {
	color: #EEE;
}

QLineEdit {
	border: 0;
	padding: 4px;
	color: #EEE;
	font-size: 11px;
	background: #404040;
	margin-bottom: 3px;
})CSS");
}

const QString StyleSheet::ScreenshotDialogRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QPushButton  {
    border: none;
	padding: 6px 20px;
    background-color: #212121;
}

QPushButton:hover {
    background-color: #555;
})CSS");
}

const QString StyleSheet::SoftwareUpdateDialogRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(#createProject {
	background: #4898ff;
	color: white;
}

#createProject:hover {
	background-color: #51a1d6;
}


QLabel {
	margin-right: 12px;
}

QPushButton, QToolButton {
	border: 0;
	padding: 5px;
	background: #444;
}

QPushButton::hover {
	background: #555;
}

QToolButton::hover {
	background: #555;
}

QLineEdit {
	background: #404040;
	padding: 5px;
})CSS");
}

const QString StyleSheet::SoftwareUpdateDialogWidget()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(background:rgba(20,20,20,1);
color:rgba(255,255,255,.95);)CSS");
}

const QString StyleSheet::SoftwareUpdateDialogTextEdit()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(background:rgba(35,35,35,1);)CSS");
}

const QString StyleSheet::SoftwareUpdateDialogClose()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QPushButton{
	padding: 5px 15px;
background:rgba(50,50,50,1);
border: 1px solid rgba(0,0,0,.1);
}

QPushButton:hover{
	background: rgba(40,128, 185,.9);

})CSS");
}

const QString StyleSheet::SoftwareUpdateDialogDownload()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QPushButton{
	padding: 5px 15px;
background:rgba(50,50,50,1);
border: 1px solid rgba(0,0,0,.1);
}

QPushButton:hover{
	background: rgba(40,128, 185,.9);

})CSS");
}

const QString StyleSheet::PreferencesDialogRoot()
{
	JAH_CLASSIC_ONLY
	return QString(R"CSS(QDialog#PreferencesDialog {
	border: 1px solid #1E1E1E;
	background: #222;
	color: #EEE;
}

QPushButton, QToolButton {
  border: 0;
  padding: 5px 15px;
  background: #444;
	color: #EEE;
}

QPushButton::hover {
  background: #555;
}

QToolButton::hover {
  background: #555;
}

QLineEdit {
  background: #404040;
  padding: 5px;
})CSS");
}

const QString StyleSheet::SplashVersionLabel()
{
	JAH_CLASSIC_ONLY
	return QString("color: white;");
}

const QString StyleSheet::SplashShaderLabel()
{
	JAH_CLASSIC_ONLY
	return QString("color: rgba(255,255,255,200);");
}

const QString StyleSheet::UpgraderDialog()
{
	JAH_CLASSIC_ONLY
	return QString("* { color: #EEE; }"
		"QDialog { background: #222222; padding: 4px; }"
		"QPushButton { background: #444; color: #EEE; border: 0; padding: 6px 10px; }"
		"QPushButton:hover { background: #555; color: #EEE; }"
		"QPushButton:pressed { background: #333; color: #EEE; }");
}

const QString StyleSheet::PublishPage()
{
	JAH_CLASSIC_ONLY
	return QString("#publishView { background: #1e1e1e; }");
}

const QString StyleSheet::PublishTitle()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 32px; font-weight: 500; color: rgba(255,255,255,0.92); background: transparent;");
}

const QString StyleSheet::PublishSubtitle()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 15px; color: rgba(255,255,255,0.55); background: transparent;");
}

const QString StyleSheet::PublishCard()
{
	JAH_CLASSIC_ONLY
	return QString("#publishCard { background: #262a31; border: 1px solid #32363e; border-radius: 10px; }");
}

const QString StyleSheet::PublishCardTitle()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 17px; font-weight: 500; color: rgba(255,255,255,0.9); background: transparent;");
}

const QString StyleSheet::PublishDetail()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 12px; color: rgba(255,255,255,0.45); background: transparent;");
}

const QString StyleSheet::PublishStatus()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 13px; color: rgba(255,255,255,0.65); background: transparent;");
}

const QString StyleSheet::PublishStatusError()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 13px; color: #e74c3c; background: transparent;");
}

const QString StyleSheet::PublishPrimaryButton()
{
	JAH_CLASSIC_ONLY
	return QString("QPushButton { background: #3498db; color: #ffffff; border: none; border-radius: 4px;"
		"              padding: 10px 26px; font-size: 14px; font-weight: 500; }"
		"QPushButton:hover { background: #4aa3df; }"
		"QPushButton:pressed { background: #2c81ba; }"
		"QPushButton:disabled { background: #2c313a; color: rgba(255,255,255,0.35); }");
}

const QString StyleSheet::PublishSecondaryButton()
{
	JAH_CLASSIC_ONLY
	return QString("QPushButton { background: #2c313a; color: rgba(255,255,255,0.85); border: none;"
		"              border-radius: 4px; padding: 10px 20px; font-size: 13px; }"
		"QPushButton:hover { background: #363c47; }"
		"QPushButton:pressed { background: #23272e; }"
		"QPushButton:disabled { background: #23262c; color: rgba(255,255,255,0.3); }");
}

const QString StyleSheet::PublishPreviewFrame()
{
	JAH_CLASSIC_ONLY
	return QString("#previewFrame { background: #14161a; border: 1px solid #32363e; border-radius: 8px; }");
}

const QString StyleSheet::PublishPreviewLabel()
{
	JAH_CLASSIC_ONLY
	return QString("font-size: 12px; color: rgba(255,255,255,0.55); background: transparent;");
}

const QString StyleSheet::PublishPreviewSlot()
{
	JAH_CLASSIC_ONLY
	return QString("background: #000;");
}

const QString StyleSheet::EffectsPageRoot()
{
	JAH_CLASSIC_ONLY
	return QString("QMainWindow::separator {width: 10px;h eight: 0px; margin: -3.5px; padding: 0px; border: 0px solid black; background: rgba(19, 19, 19, 1);}"
		"QWidget{background:rgba(32,32,32,1); color:rgba(240,240,240,1); border: 0px solid rgba(0,0,0,0);}"
		"QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9); border-radius : 2px; }"
		"QMenu::item{padding: 4px 5px 4px 10px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"

		"QTabWidget::pane{border: 1px solid rgba(0,0,0,.1);	border - top: 0px solid rgba(0,0,0,0);	}"
		"QTabWidget::tab - bar{	left: 1px; background: rgba(26,26,26,.9);	}"
		"QDockWidget::tab{	background:rgba(32,32,32,1);}"

		"QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 24px; padding: 4px;}"
		"QScrollBar::handle{ background: rgba(72, 72, 72, 1);	border-radius: 8px; width: 14px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}");
}

const QString StyleSheet::EffectsNodePropertiesPanel()
{
	JAH_CLASSIC_ONLY
	return QString("QWidget{background:rgba(32,32,32,1);}");
}

const QString StyleSheet::EffectsNodeTiles()
{
	JAH_CLASSIC_ONLY
	return QString("QListView::item{ border-radius: 2px; border: 1px solid rgba(0,0,0,.31); background: rgba(51,51,51,1); margin: 3px;  }"
		"QListView::item:selected{ background: rgba(155,155,155,1); border: 1px solid rgba(50,150,250,.1); }"
		"QListView::item:hover{ background: rgba(95,95,95,1); border: .1px solid rgba(50,150,250,.1); }"
		"QListView::text{ top : -6; }");
}

const QString StyleSheet::EffectsNodeTilesScrollBar()
{
	JAH_CLASSIC_ONLY
	return QString("QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 10px; }"
		"QScrollBar::handle{ background: rgba(72, 72, 72, 1);	border-radius: 5px;  left: 8px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}");
}

const QString StyleSheet::EffectsDock()
{
	JAH_CLASSIC_ONLY
	return QString("QDockWidget{color: rgba(250,250,250,.9); background: rgba(32,32,32,1);}"
		"QDockWidget::title{ padding: 8px; background: rgba(22,22,22,1);	border: 1px solid rgba(20,20,20, .8);	text-align: center;}"
		"QDockWidget::close-button{ background: rgba(0,0,0,0); color: rgba(200,200,200,0); icon-size: 0px; padding: 23px; }"
		"QDockWidget::float-button{ background: rgba(0,0,0,0); color: rgba(200,200,200,0); icon-size: 0px; padding: 22px; }"
		//"QDockWidget::close-button, QDockWidget::float-button{	background: rgba(10,10,10,1); color: white;padding: 0px;}"
		//"QDockWidget::close-button:hover, QDockWidget::float-button:hover{background: rgba(0,220,0,0);padding: 0px;}"
		"QComboBox::drop-down {	width: 15px;  border: none; subcontrol-position: center right;}"
		"QComboBox::down-arrow{image : url(:/images/drop-down-24.png); }");
}

const QString StyleSheet::EffectsToolBar()
{
	JAH_CLASSIC_ONLY
	return QString(""
		//"QToolBar{background: rgba(48,48,48, 1); border: .5px solid rgba(20,20,20, .8); border-bottom: 1px solid rgba(20,20,20, .8); padding: 0px;}"
		"QToolBar{ background: rgba(48,48,48,1); border-bottom: 1px solid rgba(20,20,20, .8);}"
		"QToolBar::handle:horizontal { image: url(:/icons/thandleh.png); width: 24px; }"
		//"QToolBar::handle:vertical { image: url(:/icons/thandlev.png); height: 22px;}"
		"QToolBar::separator { background: rgba(0,0,0,.2); width: 1px; height : 20px;}"
		"QToolBar::separator:horizontal { background: #272727; width: 1px; margin-left: 6px; margin-right: 6px;} "
		"QToolButton { border-radius: 2px; background: rgba(33,33,33, 1); color: rgba(250,250,250, 1); border : 1px solid rgba(10,10,10, .4); font: 18px; padding: 8px; } "
		"QToolButton:hover{ background: rgba(48,48,48, 1); } "
		"QToolButton#actionDownload{width:40px;}");
}

const QString StyleSheet::EffectsEmptySpacer()
{
	JAH_CLASSIC_ONLY
	return QString("background : rgba(0,0,0,0);");
}

const QString StyleSheet::EffectsPreviewMenu()
{
	JAH_CLASSIC_ONLY
	return QString("QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu:hover { background-color: #3498db; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 16px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; }"
		"QMenu::item : disabled { color: #555; }");
}

const QString StyleSheet::EffectsDownloadButton()
{
	JAH_CLASSIC_ONLY
	return QString("QPushButton{ background-color: rgba(33,33,33, 1); color: #DEDEDE; border : 0; padding: 10px 16px; margin-right:6px; margin-left:6px; border-radius: 2px; }"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }");
}

const QString StyleSheet::EffectsProjectName()
{
	JAH_CLASSIC_ONLY
	return QString("QLineEdit{background: rgba(0,0,0,0); border-radius: 3px; padding-left: 5px; color: rgba(255,255,255,.8); }"
		"QLineEdit:hover{ background : rgba(21,21,21,1); color: rgba(255,255,255,1);}");
}

const QString StyleSheet::MaterialsListTiles()
{
	JAH_CLASSIC_ONLY
	return QString("QListView::item{ border-radius: 2px; border: 0px solid rgba(0,0,0,1); background: rgba(80,80,80,0); margin-left: 6px;  }"
		"QListView::item:selected{ background: rgba(65,65,65,1); border: 1px solid rgba(50,150,250,1); }"
		"QListView::item:hover{ background: rgba(55,55,55,1); border: 1px solid rgba(50,150,250,1); }"
		"QListView::text{ top : -6; }");
}

const QString StyleSheet::MaterialsListScrollBar()
{
	JAH_CLASSIC_ONLY
	return QString("QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 10px; }"
		"QScrollBar::handle{ background: rgba(72, 72, 72, 1);	border-radius: 3px;  left: 8px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}");
}

const QString StyleSheet::EffectsTabbedWidget()
{
	JAH_CLASSIC_ONLY
	return QString(StyleSheet::EffectsDock() + QString(
		"QTabWidget::pane{	border: 1px solid rgba(0, 0, 0, .5); border - top: 0px solid rgba(0, 0, 0, 0);}"
		"QTabBar::tab{	background: rgba(21, 21, 21, .7); color: rgba(250, 250, 250, .9); font - weight: 400; font - size: 13em; padding: 5px 22px 5px 22px; }"
		"QTabBar::tab:selected{ color: rgba(255, 255, 255, .99); border-top: 2px solid rgba(50,150,250,.8); }"
		"QTabBar::tab:!selected{ background: rgba(55, 55, 55, .99); border : 1px solid rgba(21,21,21,.4); color: rgba(200,200,200,.5); }"));
}

const QString StyleSheet::EffectsPresetsList()
{
	JAH_CLASSIC_ONLY
	return QString(StyleSheet::MaterialsListTiles() + QString("border: 1px solid black;"));
}

const QString StyleSheet::MaterialsContextMenu()
{
	JAH_CLASSIC_ONLY
	return QString("QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
		"QMenu::item:disabled { color: #555; }");
}

const QString StyleSheet::MaterialsMutedLabel()
{
	JAH_CLASSIC_ONLY
	return QString("color: rgba(200,200,200,.55);");
}

const QString StyleSheet::MaterialsTexturePreviewButton()
{
	JAH_CLASSIC_ONLY
	return QString("background:rgba(0,0,0,.2); border: 1px solid rgba(50,50,50,.4);");
}

const QString StyleSheet::MaterialsTransparent()
{
	JAH_CLASSIC_ONLY
	return QString("background: rgba(0,0,0,0);");
}

const QString StyleSheet::MaterialsPropertyName()
{
	JAH_CLASSIC_ONLY
	return QString("QLineEdit{ background : rgba(29,29,29,0); border-bottom : 2px solid rgba(18,18,18,1); }"
		"QLineEdit:hover{ background: rgba(32,32,32,1); border: 1px solid rgba(0,0,0,1);}");
}

const QString StyleSheet::MaterialsPropertyMenu()
{
	JAH_CLASSIC_ONLY
	return QString("QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9);}"
		"QMenu::item{padding: 2px 5px 2px 20px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"
		"QCheckBox {   spacing: 2px 5px;}"
		"QCheckBox::indicator {   width: 28px;   height: 28px; }"
		"QCheckBox::indicator::unchecked {	image: url(:/icons/check-unchecked.png);}"
		"QCheckBox::indicator::checked {		image: url(:/icons/check-checked.png);}"
		"QLineEdit {	border: 0;	background: #292929;	padding: 6px;	margin: 0;}"
		"QToolButton {	background: #1E1E1E;	border: 0;	padding: 6px;}"
		"QToolButton:pressed {	background: #111;}"
		"QToolButton:hover {	background: #404040;}"
		"QDoubleSpinBox, QSpinBox {	border-radius: 1px;	padding: 6px;	background: #292929;}"
		"QDoubleSpinBox::up-arrow, QSpinBox::up-arrow {	width:0;}"
		"QDoubleSpinBox::up-button, QSpinBox::up-button, QDoubleSpinBox::down-button, QSpinBox::down-button {	width:0;}"
		"QComboBox:editable {}"
		"QComboBox QAbstractItemView::item {    show-decoration-selected: 1;}"
		"QComboBox QAbstractItemView::item {    padding: 6px;}"
		"QListView::item:selected {    background: #404040;}"
		"QComboBox:!editable, QComboBox::drop-down:editable {     background: #1A1A1A;}"
		"QComboBox:!editable:on, QComboBox::drop-down:editable:on {    background: #1A1A1A;}"
		"QComboBox QAbstractItemView {    background-color: #1A1A1A;    selection-background-color: #404040;    border: 0;    outline: none;}"
		"QComboBox QAbstractItemView::item {    border: none;    padding-left: 5px;}"
		"QComboBox QAbstractItemView::item:selected {    background: #404040;    padding-left: 5px;}"
		"QComboBox::drop-down {    subcontrol-origin: padding;    subcontrol-position: top right;    width: 18px;    border-left-width: 1px;}"
		"QComboBox::down-arrow {    image: url(:/icons/down_arrow_check.png);	width: 18px;	height: 14px;} "
		"QComboBox::down-arrow:!enabled {    image: url(:/icons/down_arrow_check_disabled.png);    width: 18px;    height: 14px;}"
		"QLabel{}"
		"QPushButton{padding : 3px; }");
}

const QString StyleSheet::MaterialsPropertyDoubleSpin()
{
	JAH_CLASSIC_ONLY
	return QString("QDoubleSpinBox{ border-radius : 1px; padding : 7px; background: #292929; }"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}");
}

const QString StyleSheet::MaterialsPropertySpin()
{
	JAH_CLASSIC_ONLY
	return QString("QSpinBox{ border-radius : 1px; padding : 7px; background: #292929; }"
		"QSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}");
}

const QString StyleSheet::MaterialSettingsPanel()
{
	JAH_CLASSIC_ONLY
	return QString("QCheckBox {   spacing: 2px 5px; width: 12px; height :12px;}"
		"QCheckBox::indicator {   width: 18px;   height: 18px; }"
		"QCheckBox::indicator::unchecked {	image: url(:/icons/check-unchecked.png);}"
		"QCheckBox::indicator::checked {		image: url(:/icons/check-checked.png);}"
		"QLineEdit {	border: 0;	background: #292929;	padding: 6px;	margin: 0;}"
		"QToolButton {	background: #1E1E1E;	border: 0;	padding: 6px;}"
		"QToolButton:pressed {	background: #111;}"
		"QToolButton:hover {	background: #404040;}"
		"QDoubleSpinBox {	border-radius: 1px;	padding: 6px;	background: #292929;}"
		"QListView::item:selected {    background: #404040;}"
		"QComboBox:editable {}"
		"QComboBox QAbstractItemView::item {    show-decoration-selected: 1;}"
		"QComboBox QAbstractItemView::item {    padding: 6px;}"
		"QComboBox  {    background-color: #1A1A1A;   border: 0;    outline: none; padding: 3px 10px; }"
		"QComboBox:!editable, QComboBox::drop-down:editable {     background: #1A1A1A;}"
		"QComboBox:!editable:on, QComboBox::drop-down:editable:on {    background: #1A1A1A;}"
		"QComboBox QAbstractItemView {    background-color: #1A1A1A;    selection-background-color: #404040;    border: 0;    outline: none; padding: 4px 10px; }"
		"QComboBox QAbstractItemView::item {    border: none; padding: 4px 10px;}"
		"QComboBox QAbstractItemView::item:selected {    background: #404040;    padding-left: 5px;}"
		"QComboBox::drop-down {    subcontrol-origin: padding;    subcontrol-position: top right;    width: 18px;    border-left-width: 1px;}"
		"QComboBox::down-arrow {    image: url(:/icons/down_arrow_check.png);	width: 18px;	height: 14px;} "
		"QComboBox::down-arrow:!enabled {    image: url(:/icons/down_arrow_check_disabled.png);    width: 18px;    height: 14px;}");
}

const QString StyleSheet::MaterialsNodeMenu()
{
	JAH_CLASSIC_ONLY
	return QString("QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9);}"
		"QMenu::item{padding: 2px 5px 2px 20px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"
		"QCheckBox {   spacing: 2px 5px;}"
		"QCheckBox::indicator {   width: 28px;   height: 28px; }"
		"QCheckBox::indicator::unchecked {	image: url(:/icons/check-unchecked.png);}"
		"QCheckBox::indicator::checked {		image: url(:/icons/check-checked.png);}"
		"QLineEdit {	border: 0;	background: #292929;	padding: 6px;	margin: 0;}"
		"QToolButton {	background: #1E1E1E;	border: 0;	padding: 6px;}"
		"QToolButton:pressed {	background: #111;}"
		"QToolButton:hover {	background: #404040;}"
		"QDoubleSpinBox, QSpinBox {	border-radius: 1px;	padding: 6px;	background: #292929;}"
		"QDoubleSpinBox::up-arrow, QSpinBox::up-arrow {	width:0;}"
		"QDoubleSpinBox::up-button, QSpinBox::up-button, QDoubleSpinBox::down-button, QSpinBox::down-button {	width:0;}"
		"QListView::item:selected {    background: #404040;}"
		"QComboBox:editable {}"
		"QComboBox QAbstractItemView::item {    show-decoration-selected: 1;}"
		"QComboBox QAbstractItemView::item {    padding: 6px;}"
		"QComboBox  {    background: rgba(0,0,0,0);   border: 2px solid rgba(0,0,0,.4);    outline: none; padding: 6px 10px; color: rgba(250,250,250,1);}"
		//	"QComboBox:!editable, QComboBox::drop-down:editable {     background: #1A1A1A;}"
		//	"QComboBox:!editable:on, QComboBox::drop-down:editable:on {    background: #1A1A1A;}"
		"QComboBox QAbstractItemView { background: rgba(0,0,0,.2);  color: rgba(250,250,250,1);  selection-background-color: #404040; border: 2px solid rgba(0,0,0,.4); outline: none; padding: 4px 10px; }"
		"QComboBox QAbstractItemView::item {    border: none; padding: 4px 10px;}"
		"QComboBox QAbstractItemView::item:selected {    background: #404040;    padding-left: 5px;}"
		"QComboBox::drop-down {    subcontrol-origin: padding;    subcontrol-position: top right;    width: 18px;    border-left-width: 1px;}"
		"QComboBox::down-arrow {    image: url(:/icons/down_arrow_check.png);	width: 18px;	height: 14px;} "
		"QComboBox::down-arrow:!enabled {    image: url(:/icons/down_arrow_check_disabled.png);    width: 18px;    height: 14px;}"
		"QLabel{}"
		"QPushButton{padding : 3px; }");
}

const QString StyleSheet::MaterialsTreeScrollBar()
{
	JAH_CLASSIC_ONLY
	return QString("QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 10px; }"
		"QScrollBar::handle{ background: rgba(62, 62, 62, 1);	border-radius: 4px;  left: 8px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}");
}

const QString StyleSheet::MaterialsNodeTextureWidget()
{
	JAH_CLASSIC_ONLY
	return QString("background:rgba(0,0,0,0); color: rgba(250,250,250,.9); padding: 0px;");
}

const QString StyleSheet::MaterialsNodeTextureThumb()
{
	JAH_CLASSIC_ONLY
	return QString("background:rgba(0,0,0,0); border : 2px solid rgba(50,50,50,.3);");
}

const QString StyleSheet::MaterialsNodeTextureWidgetAlt()
{
	JAH_CLASSIC_ONLY
	return QString("background:rgba(0,0,0,0); color: rgba(250,250,250,.9);");
}

const QString StyleSheet::MaterialsTree()
{
	JAH_CLASSIC_ONLY
	return QString("QTreeWidget { outline: none; selection-background-color: #404040; color: #EEE; }"
		"QTreeWidget::branch { background-color:rgba(0,0,0,0); }"
		"QTreeWidget::branch:hover { background-color: #303030; }"
		"QTreeView::branch:open {background-color:rgba(0,0,0,0); image: url(:/icons/expand_arrow_open.png); }"
		"QTreeView::branch:closed:has-children { background-color:rgba(0,0,0,0);image: url(:/icons/expand_arrow_closed.png); }"
		"QTreeWidget::branch:selected { background-color: #404040; }"
		"QTreeWidget::item:selected { selection-background-color: #404040;"
		"								background-color:rgba(0,0,0,0); outline: none; padding: 5px 0; }"
		/* Important, this is set for when the widget loses focus to fill the left gap */
		"QTreeWidget::item:selected:!active { background: #404040; padding: 5px 0; color: #EEE; }"
		"QTreeWidget::item:selected:active { background: #404040; padding: 5px 0; }"
		"QTreeWidget::item {background-color:rgba(0,0,0,0); padding: 5px 0; height : 10px; }"
		"QTreeWidget::item:hover { background: #303030; padding: 5px 0; }");
}

const QString StyleSheet::MaterialsNodeVectorFields()
{
	JAH_CLASSIC_ONLY
	return QString("QDoubleSpinBox{border: 2px solid rgba(200, 200, 200, .4); padding: 2px; background: rgba(0, 0, 0, 0.2);}"
		"QWidget{ background: rgba(0,0,0,0); color: rgba(250,250,250,1); }"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}");
}

const QString StyleSheet::MaterialsNodeValueBox()
{
	JAH_CLASSIC_ONLY
	return QString("QDoubleSpinBox{border: 1px solid rgba(200, 200, 200, .4); border-radius: 2px;"
		" padding: 0 2px; background: rgba(0, 0, 0, 0.35); color: rgba(250,250,250,1); font-size: 11px;}"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}");
}

const QString StyleSheet::MaterialsNodeTextureFields()
{
	JAH_CLASSIC_ONLY
	return QString("QDoubleSpinBox{border: 2px solid rgba(200, 200, 200, .4); padding: 2px; background: rgba(0, 0, 0, 0.2);}"
		"QComboBox{border: 2px solid rgba(200, 200, 200, .4); padding: 1px; background: rgba(0, 0, 0, 0.2);}"
		"QWidget{ background: rgba(0,0,0,0); color: rgba(250,250,250,1); }"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}");
}

const QString StyleSheet::CreateNewTiles()
{
	JAH_CLASSIC_ONLY
	return QString("QListView::item{ border-radius: 2px; border: 1px solid rgba(0,0,0,1); background: rgba(80,80,80,1); margin: 3px;  }"
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
		"QLabel#infoLabel{color: rgba(200,200,200,.5);}");
}

const QString StyleSheet::CreateNewSectionLabel()
{
	JAH_CLASSIC_ONLY
	return QString("QLabel{ background: rgba(20,20,20,1); padding: 3px; padding-left: 8px; color: rgba(200,200,200,1); }");
}

const QString StyleSheet::CreateNewHolder()
{
	JAH_CLASSIC_ONLY
	return QString("QWidget{background:rgba(32,32,32,1); color:rgba(240,240,240,1); border: 0px solid rgba(0,0,0,0);}"
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
		"QPushButton:checked{ background-color: rgba(50,150,255,1); }");
}

const QString StyleSheet::CreateNewTabs()
{
	JAH_CLASSIC_ONLY
	return QString("QTabWidget::pane{	border: 0px solid rgba(0, 0, 0, .5); border-top: 1px solid rgba(0, 0, 0, .4); border-bottom: 1px solid rgba(0,0,0,.4);}"
		"QTabBar::tab{	background: rgba(21, 21, 21, .7); color: rgba(250, 250, 250, .9); font - weight: 400; font - size: 13em; padding: 5px 22px 5px 22px; }"
		"QTabBar::tab:selected{ color: rgba(255, 255, 255, .99); border-top: 2px solid rgba(50,150,250,.8); }"
		"QTabBar::tab:!selected{ background: rgba(55, 55, 55, .99); border : 1px solid rgba(21,21,21,.4); color: rgba(200,200,200,.5); }");
}

const QString StyleSheet::CreateNewButtons()
{
	JAH_CLASSIC_ONLY
	return QString("QPushButton{ background: #333; color: #DEDEDE; border : 0px; padding: 4px 16px; border-radius: 3px;}"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }"
		"QPushButton:disabled{ color: #444; }"
		"QPushButton:checked{ background-color: rgba(50,150,250,.8); }"
		"QLabel{ border: 0; background: rgba(0,0,0,0); }");
}

const QString StyleSheet::SearchDialogTabs()
{
	JAH_CLASSIC_ONLY
	return QString("QTabWidget::pane{	border: 1px solid rgba(0, 0, 0, .1); border-top: 0px solid rgba(0, 0, 0, 0); padding-top: 7px; }"
		"QTabBar::tab{	background: rgba(21, 21, 21, .7); color: rgba(250, 250, 250, .9); font - weight: 400; font-size: 13em; padding: 5px 22px 5px 22px; }"
		"QTabBar::tab:selected{ color: rgba(255, 255, 255, .99); border-top: 2px solid rgba(50,150,250,.8); }"
		"QTabBar::tab:!selected{ background: rgba(55, 55, 55, .99); border : 1px solid rgba(21,21,21,.4); color: rgba(200,200,200,.5); }");
}

const QString StyleSheet::SearchDialogBarRadius()
{
	JAH_CLASSIC_ONLY
	return QString("border-radius : 2px; ");
}

const QString StyleSheet::SearchDialogContainer()
{
	JAH_CLASSIC_ONLY
	return QString("background:rgba(32,32,32,0);");
}

const QString StyleSheet::SearchDialogBar()
{
	JAH_CLASSIC_ONLY
	return QString("QLineEdit{ background:rgba(41,41,41,1); border: 1px solid rgba(150,150,150,.2); border-radius: 1px; color: rgba(250,250,250,.95); padding: 6px;  }");
}

const QString StyleSheet::SearchDialogRoot()
{
	JAH_CLASSIC_ONLY
	return QString(""
		"QWidget{background: rgba(21,21,21,1); border: 0px solid rgba(0,0,0,0);}"
		"QListView::item{color: rgba(255,255,255,1); border-radius: 2px; border: 1px solid rgba(0,0,0,.31); background: rgba(51,51,51,1); margin: 3px;  }"
		"QListView::item:selected{ background: rgba(155,155,155,1); border: 1px solid rgba(50,150,250,.1); }"
		"QListView::item:hover{ background: rgba(95,95,95,1); border: .1px solid rgba(50,150,250,.1); }"
		"QListView::text{ top : -6; }");
}

const QString StyleSheet::ShaderAssetConfirmDialog()
{
	JAH_CLASSIC_ONLY
	return QString("* { color: #EEE; }"
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
		"QListWidget::indicator::disabled { image: url(:/icons/check-disabled.png); }");
}


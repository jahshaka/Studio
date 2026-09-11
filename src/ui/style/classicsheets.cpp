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

//@@CLASSIC-DEFS@@

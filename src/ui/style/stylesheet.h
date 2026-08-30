/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>

class StyleSheet : public QObject
{
public:
	// Qlementine kill switch: false (default) = the Qlementine QStyle owns all
	// widget rendering and every getter below returns an empty sheet; true =
	// the archived "Jahshaka Classic" theme, getters return their classic CSS.
	// Set once at startup by ThemeManager::applyAtStartup.
	static bool classicThemeActive();
	static void setClassicThemeActive(bool active);

	static const QString QPushButtonBlue();
	static const QString QPushButtonBlueBig();
	static const QString QPushButtonInvisible();
    static const QString QPushButtonGreyscale();
	static const QString QPushButtonGrouped();
	static const QString QPushButtonGroupedBig();
	static const QString QPushButtonDanger();
    static const QString QPushButtonGreyscaleBig();
    static const QString QPushButtonRounded(int size);
    static const QString QSpinBox();
    static const QString QSlider();
    static const QString QLineEdit();
    static const QString QWidgetDark();
    static const QString QWidgetTransparent();
    static const QString QLabelWhite();
    static const QString QLabelBlack();
    static const QString QComboBox();
	static const QString QCheckBox();
	static const QString QSplitter();
	static const QString QAbstractScrollArea();
	static const QString QMenu();

	/* Blocks centralized out of the widgets that used to carry them inline.
	   Each returns exactly the CSS its former call sites passed. */

	// context menus. QMenuDark and QMenuDarkGrid differ ONLY in the disabled-item
	// selector: "QMenu::item : disabled" (inert, descendant form) vs
	// "QMenu::item:disabled". Kept apart so neither call site changes appearance.
	static const QString QMenuDark();
	static const QString QMenuDarkGrid();
	static const QString QMenuDarkPadded();
	static const QString QMenuDarkDesktop();
	static const QString QMenuFlat();

	// asset widget (dock)
	static const QString AssetWidgetFilterPane();
	static const QString AssetWidgetPanel();
	static const QString AssetWidgetTagDialog();

	// asset view (page)
	static const QString AssetViewCollectionDialog();
	static const QString AssetViewSearchField();
	static const QString AssetViewFilterPane();
	static const QString AssetViewImportButtons();
	static const QString AssetViewAddToProjectButton();
	static const QString AssetViewDeleteButton();
	static const QString AssetViewChangeCollectionLink();
	static const QString AssetViewMetadataHeader();
	static const QString AssetViewPanel();
	static const QString AssetViewRenameDialog();

	// scene hierarchy
	static const QString SceneHierarchyTree();

	// colour picker
	static const QString ColorViewPanel();
	static const QString ColorViewInputCircle();
	static const QString ValueSliderGradient();

	// preferences dialog
	// The whole tab container (tab bar, pane, pages): the dark theme the
	// dialog's own sheet cannot reach — a bare QTabWidget renders the
	// platform-light pane, and unstyled child labels come out black.
	static const QString PreferencesTabs();
	// Secondary/muted explanation text on the dark theme.
	static const QString MutedInfoText();

	// main window
	static const QString TopMenuDisabled();
	static const QString TopMenuSelected();
	static const QString TopMenuUnselected();
	static const QString BackgroundTransparent();
	static const QString HelpButton();
	static const QString PrefsButton();
	static const QString ControlBar();
	static const QString DockToggleDialog();
	static const QString MainWindowHeaderLogo(const QString &imagePath);

	// project tiles
	static const QString ItemGridTileButton();
	static const QString ItemGridTileControls();
	static const QString ItemGridTileCaptionActive();
	static const QString ItemGridTileCaptionIdle();
	static const QString ItemGridTileBorder(int width);
	static const QString ItemGridTileBorderHighlight(int width);
	static const QString ItemGridTileLabel(int fontSize);

	// asset grid tile
	static const QString AssetGridItemLabel(const QString &borderColor);
	static const QString AssetGridItemThumbnail(const QString &borderColor);

	// project manager
	static const QString ProjectManagerCanvas();
	static const QString ProjectManagerSampleList();
	static const QString ProjectManagerInstructions();

	static const QString QLineEditDisabled();

	static void setStyle(QWidget *);
	static void setStyle(QObject *);
	static void setStyle(QList<QWidget *>);
};



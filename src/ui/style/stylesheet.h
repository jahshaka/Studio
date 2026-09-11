/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef JAH_STYLESHEET_H
#define JAH_STYLESHEET_H

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

	/* ---- THE THEME SWEEP'S CLASSIC ARCHIVE (lane 16, classicsheets.cpp) ----
	   Every raw setStyleSheet() string and every .ui-embedded styleSheet that
	   Classic still needs, moved here VERBATIM behind the same kill switch:
	   Classic renders bit-for-bit (the identical sheet lands on the identical
	   widget), Qlementine gets "" — no QStyleSheetStyle over the style. One
	   getter per former site (or per identical string), grouped by the file
	   that calls it. */

	// main window (mainwindow.ui root sheet)
	static const QString MainWindowRoot();

	// main window (mainwindow.cpp)
	static const QString MainWindowPropertiesDock();
	static const QString BorderNone();
	static const QString MainWindowPresetsDock();
	static const QString ViewportMenuButton();
	static const QString ViewportCameraToggle();
	static const QString PlayerControlsBar();

	// accordion blades (accordionbladewidget.ui)
	static const QString AccordionBladeRoot();

	// value rows (hfloatsliderwidget.ui)
	static const QString HFloatSliderRoot();

	// combo rows (comboboxwidget.ui)
	static const QString ComboBoxWidgetRoot();

	// texture picker (texturepickerwidget.ui)
	static const QString TexturePickerRoot();

	// file picker (filepickerwidget.ui)
	static const QString FilePickerRoot();
	static const QString FilePickerFilename();

	// asset picker (assetpickerwidget.ui)
	static const QString AssetPickerRoot();
	static const QString AssetPickerAssetView();

	// scene hierarchy (scenehierarchywidget.ui)
	static const QString SceneHierarchyRoot();
	static const QString SceneHierarchyWidget();
	static const QString SceneHierarchySceneTree();

	// sky presets (skypresets.ui)
	static const QString SkyPresetsRoot();

	// timeline (animationwidget.ui)
	static const QString AnimationWidgetRoot();
	static const QString AnimationWidgetInsertFrame();

	// timeline (animationwidget.cpp)
	static const QString TimelineModeActive();
	static const QString TimelineModeIdle();

	// transform editor (transformeditor.cpp)
	static const QString TransformEditorPanel();

	// drag value rows (dragvaluewidgets.cpp)
	static const QString DragValueRowPanel();

	// particle colour ramp (particlerampwidget.cpp) — a colour swatch button
	static const QString ParticleRampSwatch(const QColor &colour);

	// tooltips (tooltip.cpp — Classic's popup; Qlementine uses the native tooltip)
	static const QString ToolTipPopup();

	// script console (scriptconsole.cpp)
	static const QString ScriptConsolePanel();

	// light panels (lightchannelswidget.cpp, lightpropertywidget.cpp)
	static const QString WarningNote();

	// presets panels (assetmodelpanel.cpp, assetmaterialpanel.cpp)
	static const QString PresetsListPanel();
	static const QString PresetsContextMenu();

	// assets page (assetview.cpp)
	static const QString AssetViewMutedLabel();
	static const QString AssetViewPreviewTitle();
	static const QString AssetViewEmptyPreview();
	static const QString AssetViewEmptyPreviewLabel();
	static const QString AssetViewLocalAssetsLabel();
	static const QString AssetViewNavPane();
	static const QString AssetViewEmptyLibraryLabel();
	static const QString AssetViewPaneBorderless();
	static const QString AssetViewTailStatus();
	static const QString AssetViewPaneBackground();
	static const QString AssetViewUpdateButton();
	static const QString AssetViewNothingSelected();
	static const QString AssetViewStoreOfflineBanner();

	// asset grid tiles (assetgriditem.cpp)
	static const QString AssetGridTile();
	static const QString AssetGridLoadingOverlay();
	static const QString AssetGridLoadingOverlayAccent();

	// video preview (videopreviewwidget.cpp)
	static const QString VideoPreviewTitle();

	// desktop tiles (itemgridwidget.cpp)
	static const QString ItemGridTileSpacer();

	// toast (toast.cpp)
	static const QString ToastPanel();

	// desktop page (projectmanager.ui, projectmanager.cpp)
	static const QString ProjectManagerRoot();

	// about dialog (aboutdialog.ui)
	static const QString AboutDialogRoot();
	static const QString AboutDialogTextBrowser();

	// donate dialog (donate.ui)
	static const QString DonateDialogRoot();
	static const QString DonateDialogBackground();
	static const QString DonateDialogCtrl();

	// progress dialog (progressdialog.ui)
	static const QString ProgressDialogRoot();
	static const QString ProgressDialogStageLabel();

	// rename project dialog (renameprojectdialog.ui)
	static const QString RenameProjectDialogRoot();

	// screenshot dialog (screenshotwidget.ui)
	static const QString ScreenshotDialogRoot();

	// software update dialog (softwareupdatedialog.ui)
	static const QString SoftwareUpdateDialogRoot();
	static const QString SoftwareUpdateDialogWidget();
	static const QString SoftwareUpdateDialogTextEdit();
	static const QString SoftwareUpdateDialogClose();
	static const QString SoftwareUpdateDialogDownload();

	// preferences dialog (preferencesdialog.ui)
	static const QString PreferencesDialogRoot();

	// splash (versionsplashscreen.cpp)
	static const QString SplashVersionLabel();
	static const QString SplashShaderLabel();

	// upgrader (upgrader.cpp)
	static const QString UpgraderDialog();

	// publish page (publishmodule.cpp)
	static const QString PublishPage();
	static const QString PublishTitle();
	static const QString PublishSubtitle();
	static const QString PublishCard();
	static const QString PublishCardTitle();
	static const QString PublishDetail();
	static const QString PublishStatus();
	static const QString PublishStatusError();
	static const QString PublishPrimaryButton();
	static const QString PublishSecondaryButton();
	static const QString PublishPreviewFrame();
	static const QString PublishPreviewLabel();
	static const QString PublishPreviewSlot();

	// materials page (effectspage.cpp)
	static const QString EffectsPageRoot();
	static const QString EffectsNodePropertiesPanel();
	static const QString EffectsNodeTiles();
	static const QString EffectsNodeTilesScrollBar();
	static const QString EffectsDock();
	static const QString EffectsToolBar();
	static const QString EffectsEmptySpacer();
	static const QString EffectsPreviewMenu();
	static const QString EffectsDownloadButton();
	static const QString EffectsProjectName();

	// materials lists (listwidget.cpp)
	static const QString MaterialsListTiles();
	static const QString MaterialsListScrollBar();
	static const QString EffectsTabbedWidget();
	static const QString EffectsPresetsList();

	// materials graph + lists (graphnodescene.cpp, listwidget.cpp; was NodeStyle::menuStyleSheet)
	static const QString MaterialsContextMenu();

	// materials node properties (nodepropertiespanel.cpp)
	static const QString MaterialsMutedLabel();
	static const QString MaterialsTexturePreviewButton();

	// materials property rows (basepropertywidget.cpp)
	static const QString MaterialsTransparent();
	static const QString MaterialsPropertyName();
	static const QString MaterialsPropertyMenu();
	static const QString MaterialsPropertyDoubleSpin();
	static const QString MaterialsPropertySpin();

	// material settings (materialsettingswidget.cpp)
	static const QString MaterialSettingsPanel();

	// materials graph nodes (nodemodel.cpp, nodes/*.cpp)
	static const QString MaterialsNodeMenu();

	// materials trees (treewidget.cpp)
	static const QString MaterialsTreeScrollBar();
	static const QString MaterialsNodeTextureWidget();
	static const QString MaterialsNodeTextureThumb();
	static const QString MaterialsNodeTextureWidgetAlt();
	static const QString MaterialsTree();
	static const QString MaterialsNodeVectorFields();
	static const QString MaterialsNodeValueBox();
	static const QString MaterialsNodeTextureFields();

	// materials create-new dialog (createnewdialog.cpp)
	static const QString CreateNewTiles();
	static const QString CreateNewSectionLabel();
	static const QString CreateNewHolder();
	static const QString CreateNewTabs();
	static const QString CreateNewButtons();

	// materials node search (searchdialog.cpp)
	static const QString SearchDialogTabs();
	static const QString SearchDialogBarRadius();
	static const QString SearchDialogContainer();
	static const QString SearchDialogBar();
	static const QString SearchDialogRoot();

	// materials shader assets (shaderassetwidget.cpp)
	static const QString ShaderAssetConfirmDialog();
//@@CLASSIC-DECLS@@
};

#endif // JAH_STYLESHEET_H



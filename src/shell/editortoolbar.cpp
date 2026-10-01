/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/editortoolbar.h"

#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QSlider>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

#include "shell/actionhost.h"
#include "shell/dockstate.h"
#include "thirdparty/qtawesome/QtAwesome.h"
#include "viewport/cameraspeed.h"
#include "viewport/ieditorviewport.h"
#include "viewport/snapsettings.h"

EditorToolbar::EditorToolbar(QObject *parent) : QObject(parent)
{
}

void EditorToolbar::build(const Deps &deps)
{
    mDeps = deps;
    sceneView = deps.viewport;
    QMainWindow *viewPort = deps.viewPort;
    QtAwesome *fontIcons = deps.icons;

	QVariantMap options;
	options.insert("color", QColor(255, 255, 255));
	options.insert("color-active", QColor(255, 255, 255));
  
    toolBar = new QToolBar("Tool Bar");
	// Named: DockState's snapshot of `viewPort` matches toolbars by objectName.
	toolBar->setObjectName(QString::fromLatin1(DockState::kEditorToolBarName));
	toolBar->setIconSize(QSize(16, 16));

	QAction *actionUndo = new QAction;
	actionUndo->setToolTip("Undo | Undo last action");
	actionUndo->setObjectName(QStringLiteral("actionUndo"));
	actionUndo->setIcon(fontIcons->icon(fa::reply, options));
	toolBar->addAction(actionUndo);

	QAction *actionRedo = new QAction;
	actionRedo->setToolTip("Redo | Redo last action");
	actionRedo->setObjectName(QStringLiteral("actionRedo"));
	actionRedo->setIcon(fontIcons->icon(fa::share, options));
	toolBar->addAction(actionRedo);

	toolBar->addSeparator();

	connect(actionUndo, &QAction::triggered, this, [this]() { if (mDeps.undo) mDeps.undo(); });
	connect(actionRedo, &QAction::triggered, this, [this]() { if (mDeps.redo) mDeps.redo(); });

    actionTranslate = new QAction;
    actionTranslate->setObjectName(QStringLiteral("actionTranslate"));
    actionTranslate->setCheckable(true);
	actionTranslate->setToolTip("Translate | Manipulator for translating objects | Translates the object along a given axis");
	actionTranslate->setIcon(fontIcons->icon(fa::arrows, options));
	toolBar->addAction(actionTranslate);

    actionRotate = new QAction;
    actionRotate->setObjectName(QStringLiteral("actionRotate"));
    actionRotate->setCheckable(true);
	actionRotate->setToolTip("Rptate | Manipulator for rotating objects | Rotates the object along a given axis");
	actionRotate->setIcon(fontIcons->icon(fa::rotateright, options));
	toolBar->addAction(actionRotate);

    actionScale = new QAction;
    actionScale->setObjectName(QStringLiteral("actionScale"));
    actionScale->setCheckable(true);
	actionScale->setToolTip("Scale | Manipulator for scaling objects | Scales the object along a given axis");
	actionScale->setIcon(fontIcons->icon(fa::expand, options));
	toolBar->addAction(actionScale);

    toolBar->addSeparator();

    actionGlobalSpace = new QAction;
    actionGlobalSpace->setObjectName(QStringLiteral("actionGlobalSpace"));
    actionGlobalSpace->setCheckable(true);
	actionGlobalSpace->setToolTip("Global Space | Move objects relative to the global world");
	actionGlobalSpace->setIcon(fontIcons->icon(fa::globe, options));
	toolBar->addAction(actionGlobalSpace);

    actionLocalSpace = new QAction;
    actionLocalSpace->setObjectName(QStringLiteral("actionLocalSpace"));
    actionLocalSpace->setCheckable(true);
	actionLocalSpace->setToolTip("Local Space | Move objects relative to their transform");
	actionLocalSpace->setIcon(fontIcons->icon(fa::cube, options));
	toolBar->addAction(actionLocalSpace);

    toolBar->addSeparator();

    QAction *actionFreeCamera = new QAction;
    actionFreeCamera->setObjectName(QStringLiteral("actionFreeCamera"));
    actionFreeCamera->setCheckable(true);
	actionFreeCamera->setToolTip("Free Camera | Freely move and orient the camera");
	actionFreeCamera->setIcon(fontIcons->icon(fa::eye, options));
	toolBar->addAction(actionFreeCamera);

	QAction *actionArcballCam = new QAction;
	actionArcballCam->setObjectName(QStringLiteral("actionArcballCam"));
	actionArcballCam->setCheckable(true);
	actionArcballCam->setToolTip("Arc Ball Camera | Move and orient the camera around a fixed point | With this button selected, you are now able to move around a fixed point.");
	actionArcballCam->setIcon(fontIcons->icon(fa::dotcircleo, options));
	toolBar->addAction(actionArcballCam);

	// THE CAMERA SPEED (owner R15). ONE integer 1..32 for every way a person
	// moves through a scene — the RMB fly, the Player's free camera and a VR
	// wearer — shown on a compact toolbar button beside the two camera-mode
	// buttons it belongs with. The value lives in CameraSpeed (persisted as the
	// preference camera/speed) and the verb editor.cameraSpeed owns writing it;
	// the button, the popover's two controls and the scroll wheel are four
	// views of that one number, so they can never disagree.
	//
	// A BUTTON WITH A POPOVER, not a dropdown of fixed rungs: 32 menu entries
	// would be a scroll, and the owner asked for a slider with a number field
	// beside it. A QMenu carrying a QWidgetAction is the house's popover (the
	// theme already styles one — ThemeManager puts its Switch rows in one), and
	// it costs the toolbar less width than the ladder combo it replaces, which
	// the 1366 x 768 floor (ui.window_minimum) cares about.
	cameraSpeedButton = new QToolButton;
	cameraSpeedButton->setObjectName(QStringLiteral("cameraSpeedButton"));
	cameraSpeedButton->setToolTip("Camera Speed | One speed for the editor fly, the Player and "
	                              "VR: 1 to 32, where 10 is normal. Scroll the wheel while "
	                              "holding the right mouse button in the viewport (Shift steps "
	                              "by five).");
	cameraSpeedButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	cameraSpeedButton->setPopupMode(QToolButton::InstantPopup);
	cameraSpeedButton->setFocusPolicy(Qt::NoFocus);   // never steal the fly keys
	cameraSpeedButton->setAutoRaise(true);

	auto *speedMenu = new QMenu(cameraSpeedButton);
	auto *speedPanel = new QWidget(speedMenu);
	auto *speedRow = new QHBoxLayout(speedPanel);
	speedRow->setContentsMargins(10, 6, 10, 6);
	speedRow->setSpacing(8);
	cameraSpeedSlider = new QSlider(Qt::Horizontal, speedPanel);
	cameraSpeedSlider->setObjectName(QStringLiteral("cameraSpeedSlider"));
	cameraSpeedSlider->setRange(CameraSpeed::kMin, CameraSpeed::kMax);
	cameraSpeedSlider->setPageStep(5);
	cameraSpeedSlider->setMinimumWidth(180);
	cameraSpeedSpin = new QSpinBox(speedPanel);
	cameraSpeedSpin->setObjectName(QStringLiteral("cameraSpeedSpin"));
	cameraSpeedSpin->setRange(CameraSpeed::kMin, CameraSpeed::kMax);
	cameraSpeedSpin->setKeyboardTracking(false);   // 3 on the way to 30 is not a speed
	speedRow->addWidget(cameraSpeedSlider, 1);
	speedRow->addWidget(cameraSpeedSpin, 0);
	auto *speedAction = new QWidgetAction(speedMenu);
	speedAction->setDefaultWidget(speedPanel);
	speedMenu->addAction(speedAction);
	cameraSpeedButton->setMenu(speedMenu);

	// LIVE, both of them: dragging the slider moves the number and the camera
	// at the same time (the owner's ask), and each control writes through the
	// one setter so the other follows from syncCameraSpeedUi rather than from a
	// second copy of the value.
	connect(cameraSpeedSlider, &QSlider::valueChanged, this, [this](int n) {
		if (n == CameraSpeed::value()) return;
		CameraSpeed::setValue(n);
		syncCameraSpeedUi();
	});
	connect(cameraSpeedSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int n) {
		if (n == CameraSpeed::value()) return;
		CameraSpeed::setValue(n);
		syncCameraSpeedUi();
	});
	toolBar->addWidget(cameraSpeedButton);

	// THE BUTTON FOLLOWS THE DIAL BY CONSTRUCTION, not by every caller
	// remembering to say so (fix round item 1): the Player's wheel writes the
	// same one value from a page with no toolbar of its own, and used to leave
	// this button reading the old number until something else moved it — the
	// per-controller hook it called was assigned nowhere. Every writer — both
	// wheels, the popover, the verb — announces through here now.
	CameraSpeed::setOnChanged([this] { syncCameraSpeedUi(); });
	syncCameraSpeedUi();

	// ...AND THE POPOVER CLOSING IS A GESTURE ENDING: the store write is
	// deferred (CameraSpeed::flush's note) so a slider drag is not one durable
	// rewrite of jahsettings.ini per mouse-move, and this is where a drag with
	// this window's hand on it is over.
	connect(speedMenu, &QMenu::aboutToHide, this, [] { CameraSpeed::flush(); });
	connect(cameraSpeedSlider, &QSlider::sliderReleased, this, [] { CameraSpeed::flush(); });

	toolBar->addSeparator();

    connect(actionTranslate,    &QAction::triggered, this, &EditorToolbar::translateGizmo);
    connect(actionRotate,       &QAction::triggered, this, &EditorToolbar::rotateGizmo);
    connect(actionScale,        &QAction::triggered, this, &EditorToolbar::scaleGizmo);

    transformGroup = new QActionGroup(viewPort);
    transformGroup->addAction(actionTranslate);
    transformGroup->addAction(actionRotate);
    transformGroup->addAction(actionScale);
    actionTranslate->setChecked(true);

    connect(actionGlobalSpace,  &QAction::triggered, this, &EditorToolbar::useGlobalTransform);
    connect(actionLocalSpace,   &QAction::triggered, this, &EditorToolbar::useLocalTransform);

    transformSpaceGroup = new QActionGroup(viewPort);
    transformSpaceGroup->addAction(actionGlobalSpace);
    transformSpaceGroup->addAction(actionLocalSpace);
    // The toolbar starts on whatever the GIZMOS actually are, not on a guess.
    // It used to hard-check Global while Gizmo's constructor left every gizmo
    // in LOCAL space and nothing ever reconciled the two — the buttons lied
    // until the user clicked one (found writing editor.gizmoSpace, F12).
    if (gizmoTransformSpace() == QLatin1String("local")) actionLocalSpace->setChecked(true);
    else                                                 actionGlobalSpace->setChecked(true);

    connect(actionFreeCamera,   &QAction::triggered, this, &EditorToolbar::useFreeCamera);
    connect(actionArcballCam,   &QAction::triggered, this, &EditorToolbar::useArcballCam);

    cameraGroup = new QActionGroup(viewPort);
    cameraGroup->addAction(actionFreeCamera);
    cameraGroup->addAction(actionArcballCam);
    actionFreeCamera->setChecked(true);

    // THE VR SLOT: the VR module's toggle lands here, beside the camera
    // controls it belongs with (its contribution).
    if (deps.actions) deps.actions->addToolbarSlot(toolBar, QStringLiteral("editor.vr"));

    // this acts as a spacer
    QWidget* empty = new QWidget();
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toolBar->addWidget(empty);

	QAction *actionExport = new QAction;
	actionExport->setObjectName(QStringLiteral("actionExport"));
	actionExport->setCheckable(false);
	actionExport->setToolTip("Export | Export the current scene");
	actionExport->setIcon(fontIcons->icon(fa::upload, options));
	toolBar->addAction(actionExport);

	actionSaveScene = new QAction;
	actionSaveScene->setObjectName(QStringLiteral("actionSaveScene"));
	// ALWAYS THERE (owner, 2026-09-18: "show it even with auto save, as I may
	// want a force save"). Its visibility used to be `!auto_save`, which — with
	// auto-save ON by default — meant the Save button was hidden on every
	// default install: the one control that lets somebody write the world down
	// AT THE MOMENT THEY CHOOSE was missing, and nothing said why. Auto-save
	// keeps its own behaviour; this is a force save on top of it.
	actionSaveScene->setVisible(true);
	actionSaveScene->setCheckable(false);
	actionSaveScene->setToolTip("Save | Save the current scene");
	actionSaveScene->setIcon(fontIcons->icon(fa::floppyo, options));
	toolBar->addAction(actionSaveScene);

	QAction *viewDocks = new QAction;
	viewDocks->setObjectName(QStringLiteral("viewDocks"));
	viewDocks->setCheckable(false);
	viewDocks->setToolTip("Toggle Widgets | Toggle the dock widgets");
	viewDocks->setIcon(fontIcons->icon(fa::listalt, options));
	toolBar->addAction(viewDocks);

	// THE TOOLBAR'S END SLOT: contributions land here, after the shell's own
	// actions (the Claude assistant's chat button).
	if (deps.actions) deps.actions->addToolbarSlot(toolBar, QStringLiteral("editor.end"));

	// The scroll wheel stepped the camera speed while the EDITOR's camera was
	// flying: show the new number over the viewport. The toolbar button needs
	// no telling — it follows the dial itself (CameraSpeed::setOnChanged,
	// installed above) — and this signal exists for the TOAST, which is
	// anchored to this viewport and therefore belongs to this gesture alone.
	connect(sceneView->events(), &EditorViewportEvents::cameraSpeedChanged, this, [this]() {
		if (mDeps.toast) mDeps.toast("Camera Speed",
		                  QString("%1  (%2 u/s)")
		                      .arg(CameraSpeed::value())
		                      .arg(double(CameraSpeed::editorSpeed())));
	});
	
	connect(actionExport, &QAction::triggered, this, [this]() { if (mDeps.exportScene) mDeps.exportScene(); });
	connect(viewDocks, &QAction::triggered, this, [this]() { if (mDeps.toggleDocks) mDeps.toggleDocks(); });
	connect(actionSaveScene, &QAction::triggered, this, [this]() { if (mDeps.saveScene) mDeps.saveScene(); });

    viewPort->addToolBar(toolBar);
}

QVariantList EditorToolbar::toolbarActions() const
{
    QVariantList out;
    if (!toolBar) return out;
    for (const QAction *a : toolBar->actions()) {
        if (a->isSeparator()) continue;
        QString id = a->objectName();
        if (id.startsWith(QLatin1String("action"))) id = id.mid(6);
        if (id.isEmpty()) continue;
        id = id.left(1).toLower() + id.mid(1);
        out.append(QVariantMap{ { QStringLiteral("id"), id },
                                { QStringLiteral("visible"), a->isVisible() },
                                { QStringLiteral("enabled"), a->isEnabled() },
                                { QStringLiteral("tooltip"), a->toolTip() } });
    }
    return out;
}

void EditorToolbar::translateGizmo()
{
    sceneView->setGizmoLoc();
    actionTranslate->setChecked(true);
}

void EditorToolbar::rotateGizmo()
{
    sceneView->setGizmoRot();
    actionRotate->setChecked(true);
}

void EditorToolbar::scaleGizmo()
{
    sceneView->setGizmoScale();
    actionScale->setChecked(true);
}

// Space: translate -> rotate -> scale -> translate (Unreal's mode cycle).
// Routed through the same slots the toolbar uses so the checked states follow.
void EditorToolbar::cycleGizmoMode()
{
    const QString mode = sceneView->gizmoMode();
    if (mode == "translate")   rotateGizmo();
    else if (mode == "rotate") scaleGizmo();
    else                       translateGizmo();
}

void EditorToolbar::useLocalTransform()
{
    sceneView->setGizmoTransformToLocal();
    if (actionLocalSpace) actionLocalSpace->setChecked(true);
}

void EditorToolbar::useGlobalTransform()
{
    sceneView->setGizmoTransformToGlobal();
    if (actionGlobalSpace) actionGlobalSpace->setChecked(true);
}

QString EditorToolbar::gizmoTransformSpace() const
{
    return sceneView ? sceneView->gizmoTransformSpace() : QStringLiteral("global");
}

bool EditorToolbar::applyGizmoTransformSpace(const QString &space)
{
    if (space == QLatin1String("local"))       useLocalTransform();
    else if (space == QLatin1String("global")) useGlobalTransform();
    else return false;
    return true;
}

void EditorToolbar::useFreeCamera()
{
    sceneView->setFreeCameraMode();
}

void EditorToolbar::useArcballCam()
{
    sceneView->setArcBallCameraMode();
}

// THE SPEED BUTTON AND ITS POPOVER follow CameraSpeed, never the other way
// round (API-first: editor.cameraSpeed is the verb, these are views of its
// value). Reached from four directions — the slider, the number field, the
// scroll wheel while flying (EditorViewportEvents::cameraSpeedChanged) and the
// verb (invoked by name) — so both signals are blocked while the controls are
// written or the first two would fight.
void EditorToolbar::syncCameraSpeedUi()
{
    const int n = CameraSpeed::value();
    if (cameraSpeedButton) cameraSpeedButton->setText(QString::number(n));
    if (cameraSpeedSlider) {
        QSignalBlocker blocked(cameraSpeedSlider);
        cameraSpeedSlider->setValue(n);
    }
    if (cameraSpeedSpin) {
        QSignalBlocker blocked(cameraSpeedSpin);
        cameraSpeedSpin->setValue(n);
    }
}

// [ / ]: steps the ACTIVE gizmo's snap size through its step list — the
// translate size is also the ground grid's spacing, which re-spaces live.
// A toast over the viewport shows the new value (EDITOR_SHORTCUTS_SPEC §4).
void EditorToolbar::stepSnapSize(int direction)
{
    if (!mDeps.editorActive || !mDeps.editorActive()) return;
    const QString mode = sceneView->gizmoMode();
    QString text;
    if (mode == "rotate") {
        SnapSettings::setRotateSize(SnapSettings::stepped(SnapSettings::rotateSteps(),
                                                          SnapSettings::rotateSize(), direction));
        text = QString("Rotate snap: %1\xc2\xb0").arg(double(SnapSettings::rotateSize()));
    } else if (mode == "scale") {
        SnapSettings::setScaleSize(SnapSettings::stepped(SnapSettings::scaleSteps(),
                                                         SnapSettings::scaleSize(), direction));
        text = QString("Scale snap: %1").arg(double(SnapSettings::scaleSize()));
    } else {
        SnapSettings::setTranslateSize(SnapSettings::stepped(SnapSettings::translateSteps(),
                                                             SnapSettings::translateSize(), direction));
        text = QString("Move / grid snap: %1").arg(double(SnapSettings::translateSize()));
    }
    if (mDeps.toast) mDeps.toast("Snap Size", text);
}

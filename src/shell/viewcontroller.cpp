/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/viewcontroller.h"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

#include <algorithm>

#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "ui/style/stylesheet.h"
#include "viewport/ieditorviewport.h"

ViewController::ViewController(QObject *parent) : QObject(parent)
{
}

void ViewController::setViewport(IEditorViewport *viewport)
{
    mViewport = viewport;
    if (!mViewport) return;
	// A REPORT, NOT A COMMAND. The viewport telling the toolbar what its camera
	// now is must only repaint the button — routing it through the projection
	// toggle would make every such report re-issue a view change (and, since
	// the change is a canonical view now, snap the camera).
	connect(mViewport->events(), &EditorViewportEvents::updateToolbarButton, this, [this]() {
		if (!mViewport || !mViewport->editorCamera()) return;
		syncProjectionButton(mViewport->editorCamera()->isPerspective);
		setViewsButtonLabel(mViewport->cameraView());
	});
}

ViewController::CameraControls ViewController::createCameraControls()
{
    CameraControls controls;

	controls.projection = new QPushButton;
	mProjection = controls.projection;
	controls.projection->setStyleSheet(StyleSheet::ViewportCameraToggle());
	// The icon used to appear only after the first changeProjection() call —
	// invisible on a transparent background, but an empty grey pill under the
	// chrome button spec. The editor camera starts perspective; say so.
	controls.projection->setIcon(QIcon(":/icons/perspective-view-80.png"));
	controls.projection->setToolTip(tr("Perspective view | Toggle to switch to orthogonal view"));
	controls.projection->setIconSize(QSize(17, 17));
	connect(controls.projection, &QPushButton::clicked, this, [this]() {
		if (mViewport && mViewport->editorCamera())
			changeProjection(!mViewport->editorCamera()->isPerspective);
	});

    // Views ▾ — canonical camera views (owner request): Perspective plus the
    // six orthographic axis views. Same path as the view.* shortcuts and the
    // editor.setView verb (applyCameraView).
    controls.views = new QToolButton;
    mViewsButton = controls.views;
    controls.views->setStyleSheet(StyleSheet::ViewportMenuButton());
    mViewsMenu = new QMenu;
    mViewsMenu->setStyleSheet(StyleSheet::QMenuFlat());
    auto viewsGroup = new QActionGroup(mViewsMenu);
    viewsGroup->setExclusive(true);
    const QVector<QPair<QString, QString>> canonicalViews = {
        { QStringLiteral("perspective"), QStringLiteral("Perspective") },
        { QStringLiteral("top"), QStringLiteral("Top") },
        { QStringLiteral("bottom"), QStringLiteral("Bottom") },
        { QStringLiteral("left"), QStringLiteral("Left") },
        { QStringLiteral("right"), QStringLiteral("Right") },
        { QStringLiteral("front"), QStringLiteral("Front") },
        { QStringLiteral("back"), QStringLiteral("Back") },
    };
    for (const auto &entry : canonicalViews) {
        QAction *action = mViewsMenu->addAction(entry.second);
        action->setCheckable(true);
        action->setChecked(entry.first == QLatin1String("perspective"));
        action->setData(entry.first);
        viewsGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, entry]() { applyCameraView(entry.first); });
        mViewsActions.push_back(action);
    }
    controls.views->setMenu(mViewsMenu);
    // The button SHOWS the current view, it does not advertise the menu: a
    // static "Views" label told the user nothing about which view they were
    // in (owner report 2026-09-07). It starts on Perspective — the viewport's
    // own starting view — and follows every path that changes it, the
    // dropdown, the view.* shortcuts and editor.setView alike, because they
    // all land in applyCameraView.
    controls.views->setToolTip(tr("Canonical camera views"));
    setViewsButtonLabel(QStringLiteral("perspective"));
    controls.views->setPopupMode(QToolButton::InstantPopup);

    // Camera ▾ — the switcher (CAMERAS_SPEC D4): the Viewport (explorer) plus
    // every scene camera by name. Choosing a camera PILOTS it; choosing
    // Viewport ejects. It is rebuilt on every open rather than kept in sync,
    // because the list is the document's and the document changes underneath it
    // (a camera added, renamed, deleted, a whole world closed) — and a stale
    // entry would hand the viewport a stale node.
    controls.cameras = new QToolButton;
    mCamerasButton = controls.cameras;
    controls.cameras->setStyleSheet(StyleSheet::ViewportMenuButton());
    mCamerasMenu = new QMenu;
    mCamerasMenu->setStyleSheet(StyleSheet::QMenuFlat());
    connect(mCamerasMenu, &QMenu::aboutToShow, this, &ViewController::rebuildCamerasMenu);
    controls.cameras->setMenu(mCamerasMenu);
    controls.cameras->setText("Camera ");
    controls.cameras->setPopupMode(QToolButton::InstantPopup);
    controls.cameras->setToolTip(tr("Render the viewport through the free explorer or a scene camera "
                                    "(choosing a camera pilots it)"));
    return controls;
}

void ViewController::followViewport()
{
    if (mViewport) setViewsButtonLabel(mViewport->cameraView());
}

// The camera switcher's list (CAMERAS_SPEC D4). Built on every open from the
// live document; the checkmark shows what the viewport is actually rendering
// through, which is the piloted camera or the explorer.
void ViewController::rebuildCamerasMenu()
{
    if (!mCamerasMenu) return;
    mCamerasMenu->clear();
    auto group = new QActionGroup(mCamerasMenu);
    group->setExclusive(true);

    const iris::CameraNodePtr piloted = mViewport ? mViewport->pilotedCamera()
                                                  : iris::CameraNodePtr();
    QAction *explorer = mCamerasMenu->addAction(tr("Viewport"));
    explorer->setCheckable(true);
    explorer->setChecked(piloted.isNull());
    group->addAction(explorer);
    connect(explorer, &QAction::triggered, this,
            [this]() { if (mViewport) mViewport->pilotCamera(iris::CameraNodePtr()); });

    auto scene = mViewport ? mViewport->getScene() : iris::ScenePtr();
    if (!scene || scene->cameras.isEmpty()) {
        QAction *none = mCamerasMenu->addAction(tr("No scene cameras"));
        none->setEnabled(false);
        return;
    }
    mCamerasMenu->addSeparator();
    // By NAME, and stable: a QHash's order is not, and a menu that reshuffles
    // between opens is unusable.
    QVector<iris::CameraNodePtr> cameras;
    for (const auto &cam : scene->cameras) if (cam) cameras.push_back(cam);
    std::sort(cameras.begin(), cameras.end(),
              [](const iris::CameraNodePtr &a, const iris::CameraNodePtr &b) {
                  if (a->getName() != b->getName()) return a->getName() < b->getName();
                  return a->getGUID() < b->getGUID();
              });
    for (const iris::CameraNodePtr &cam : cameras) {
        QAction *action = mCamerasMenu->addAction(
            cam->getName().isEmpty() ? tr("Camera") : cam->getName());
        action->setCheckable(true);
        action->setChecked(piloted == cam);
        group->addAction(action);
        const QString guid = cam->getGUID();
        connect(action, &QAction::triggered, this, [this, guid]() {
            if (!mViewport) return;
            auto sc = mViewport->getScene();
            if (!sc) return;
            if (auto target = sc->cameras.value(guid)) mViewport->pilotCamera(target);
        });
    }
}

bool ViewController::applyCameraView(const QString &name)
{
    if (!mViewport || !mViewport->setCameraView(name)) return false;

    // The projection button is a VIEW of the state, so it is updated and never
    // asked to re-apply anything (it used to call changeProjection, which is
    // now the command and would recurse).
    const bool perspective = (name == QLatin1String("perspective"));
    syncProjectionButton(perspective);
    // ...and an axis view is remembered, so the toggle's "orthographic" means
    // "back to the one I was in".
    if (!perspective) mLastOrthographicView = name;

    for (QAction *action : mViewsActions)
        action->setChecked(action->data().toString() == name);
    setViewsButtonLabel(name);
    return true;
}

void ViewController::setViewsButtonLabel(const QString &view)
{
    if (!mViewsButton) return;
    // The label is the checked action's own text, so the button and the menu
    // can never spell the same view differently.
    for (QAction *action : mViewsActions) {
        if (action->data().toString() != view) continue;
        mViewsButton->setText(action->text() + QStringLiteral(" "));
        return;
    }
    mViewsButton->setText(QStringLiteral("Perspective "));
}

// THE PROJECTION TOGGLE IS A VIEW CHANGE (hygiene lane, 2026-09-09).
//
// Three defects in one small function, all of them the same mistake — it did
// the work itself instead of asking the viewport:
//
//  1. It wrote `mViewport->getScene()->camera`, the SCENE's camera node. The
//     explorer this viewport flies is a different node (EngineSceneViewport::
//     editorCamera), so the button changed a camera nothing was looking
//     through and the picture did not change at all until something else
//     happened to re-push.
//  2. It never went through setCameraView, so the AXIS-VIEW ROTATION LOCK was
//     never armed or disarmed (the lock reads the projection precisely because
//     this button used to bypass it — enginesceneviewport.cpp says so at
//     cameraRotationLocked) and the per-view camera memory was not consulted.
//  3. It left the Views label reading "Perspective" over an orthographic
//     picture, because only applyCameraView relabels it.
//
// So it now asks for a canonical view, and "orthographic" means the last AXIS
// view this window was in (Top on a fresh window). That is the same state the
// Views menu produces, which is the point: two controls that mean the same
// thing must not be able to leave the editor in two different states.
void ViewController::changeProjection(bool val)
{
	applyCameraView(val ? QStringLiteral("perspective") : mLastOrthographicView);
}

void ViewController::syncProjectionButton(bool perspective)
{
	if (!mProjection) return;
	if (perspective) {
		mProjection->setIcon(QIcon(":/icons/perspective-view-80.png"));
		mProjection->setToolTip(tr("Perspective view | Toggle to switch to orthogonal view"));
	} else {
		mProjection->setIcon(QIcon(":/icons/orthogonal-view-80.png"));
		mProjection->setToolTip(tr("Orthogonal view | Toggle to switch to perspective view"));
	}
}
